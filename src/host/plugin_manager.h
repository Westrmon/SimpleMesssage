// SimpleMessage — 插件管理器：发现、校验、加载、生命周期
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sm/plugin.h"
#include "sm/types.h"

#include "host/config.h"
#include "host/plugin_host.h"
#include "host/router.h"
#include "host/seh_guard.h"

namespace sm {
namespace platform {
class IModule;
}  // namespace platform

namespace host {

enum class PluginState : std::int32_t {
  Discovered = 0,
  Rejected = 1,
  Loaded = 2,
  Initialized = 3,
  InitializeFailed = 4,
  Running = 5,
  Stopped = 6,
  Disabled = 7,
  Unloaded = 8,
};

const char* PluginStateText(PluginState state);

struct PluginInfo {
  std::string    id;
  std::string    name;
  std::string    version;
  std::string    author;
  std::string    description;
  std::string    path;
  std::string    host_range;
  sm::PluginKind kind = sm::PluginKind::Source;
  PluginState    state = PluginState::Discovered;
  std::string    reason;
  bool           enabled = true;
  bool           auto_disabled = false;

  sm::Span<const sm::SettingDef> settings{};
  sm::Span<const char*>          subscriptions{};

  std::size_t setting_count = 0;

  std::int64_t last_start_unix_ms = 0;
  sm::StartResult last_start_result = sm::StartResult::Empty;
  bool            last_start_crashed = false;
  std::int64_t    last_notify_unix_ms = 0;
  bool            last_notify_crashed = false;
};

class IPluginEventSink {
 public:
  virtual ~IPluginEventSink() = default;

  virtual void OnPluginStateChanged(const std::string& plugin_id, PluginState state,
                                    const char* reason, bool crashed) = 0;
};

class PluginManager : public IPluginRunner,
                      public INotifierDelivery,
                      public IActionRunner {
 public:
  struct Stats {
    int scanned = 0;
    int accepted = 0;
    int rejected = 0;
    int enabled = 0;
    int loaded = 0;
    int running = 0;
    int stopped = 0;
    int disabled = 0;
    int initialize_failed = 0;
  };

  explicit PluginManager(config::ConfigStore& config);
  ~PluginManager();
  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

  void SetScheduleSink(IScheduleSink* sink);
  void SetUiSession(IUiSession* session);
  void SetActionSink(IPluginActionSink* sink);
  void SetEventSink(IPluginEventSink* sink);

  void Discover(const std::string& plugin_dir);

  void LoadEnabled();

  void ReleaseDiscoveredModules();

  bool Enable(const std::string& id);
  bool Disable(const std::string& id);
  bool RetryInitialize(const std::string& id);

  void ShutdownAll();

  StartCall RunStart(const std::string& id) override;
  void DisablePlugin(const std::string& id, const char* reason, bool crashed) override;

  bool InvokeOnNotify(const std::string& id, const sm::Notification& notification);
  bool Deliver(const std::string& notifier_id, const sm::Notification& n) override;
  bool InvokeOnAction(const std::string& id, const sm::Notification& notification,
                      const char* action_id) override;
  bool InvokeDrawUi(const std::string& id, sm::Span<const sm::Notification> history);
  bool InvokeDrawView(const std::string& id);

  std::size_t PumpMainThread();

  std::vector<PluginInfo> Snapshot() const;
  std::vector<NotifierSubscriptions> ActiveNotifierSubscriptions() const;
  Stats                   SnapshotStats() const;
  PluginState             StateOf(const std::string& id) const;
  sm::IPlugin* InstanceOf(const std::string& id) const;
  bool         HasPlugin(const std::string& id) const;

  void BeginShutdown();
  bool shutting_down() const { return shutting_down_.load(); }

  std::size_t main_thread_queue_size() const { return post_queue_.Size(); }

 private:
  struct Record;

  struct ValidationResult {
    bool                     ok = false;
    std::string              reason;
    std::vector<const char*> subscriptions;
  };

  Record* AddRecord(const std::string& id, const std::string& path);
  Record* FindRecord(const std::string& id) const;
  void TryLoadAndValidate(Record& record);
  ValidationResult Validate(const Record& record, const sm::PluginMeta& meta);
  PluginHostSetup  MakeHostSetup(const Record& record) const;
  void RejectRecord(Record& record, const std::string& reason);

  void CreateAndInitialize(Record& record);
  bool InitializeInstance(Record& record);
  void SetState(Record& record, PluginState state, const char* reason,
                bool crashed = false);
  void CallStop(Record& record);
  void CallExit(Record& record);
  void DestroyInstance(Record& record);
  void Unload(Record& record);
  bool FinishTeardown(Record& record);

  void EndCallFailed(const std::string& id, const char* method, const guard::Outcome& outcome);

  config::ConfigStore& config_;
  IScheduleSink*       scheduler_ = nullptr;
  IUiSession*          ui_ = nullptr;
  IPluginActionSink*   actions_ = nullptr;
  IPluginEventSink*    events_ = nullptr;

  std::string                          plugin_dir_;
  std::vector<std::unique_ptr<Record>> records_;
  std::map<std::string, Record*>       index_;

  Arena          main_arena_;
  Arena          sched_arena_;
  mutable MainThreadQueue post_queue_;

  std::atomic<bool> shutting_down_{false};
  bool              discovered_ = false;
};

}  // namespace host
}  // namespace sm

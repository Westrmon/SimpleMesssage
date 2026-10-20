// SimpleMessage — 插件管理器：发现、校验、加载、生命周期
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
  // 运行时身份：对实例行是实例 id，对类型行（零实例/被拒绝）是类型 id。
  std::string    id;
  std::string    name;          // 显示名
  std::string    type_id;
  std::string    type_name;
  std::string    custom_name;   // 配置里的 name；空表示自动名
  std::string    settings_path;
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
  std::size_t instance_ordinal = 0;  // 1 起；类型行是 0
  std::size_t instance_count = 0;    // 该类型当前的实例数
  bool        is_instance = false;
  bool        is_rejected_type = false;
  bool        can_add_instance = false;
  bool        can_delete_instance = false;
  bool        pending_removal = false;

  std::int64_t last_start_unix_ms = 0;
  sm::StartResult last_start_result = sm::StartResult::Empty;
  bool            last_start_crashed = false;
  std::int64_t    last_notify_unix_ms = 0;
  bool            last_notify_crashed = false;
};

struct PluginTypeInfo {
  std::string    type_id;
  std::string    name;
  std::string    path;
  std::string    host_range;
  sm::PluginKind kind = sm::PluginKind::Source;
  PluginState    state = PluginState::Discovered;
  std::string    reason;
  std::size_t    instance_count = 0;
  bool           module_loaded = false;
  bool           can_add_instance = false;
};

struct AddInstanceResult {
  bool        ok = false;
  std::string instance_id;
  std::string error;
};

struct ReloadResult {
  bool        ok = false;
  std::string error;
  int         scanned = 0;      // 目录里的插件 DLL 数（不含共享依赖）
  int         added = 0;        // 本次新发现的 DLL 数
  int         accepted = 0;     // 新 DLL 中通过校验并登记的数
  int         rejected = 0;     // 新 DLL 中未通过校验、被跳过的数
  int         recovered = 0;    // 之前被拒绝、这次通过校验的类型数
  int         instances_loaded = 0;  // 本次新创建并初始化的实例数
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
  void SetIconProvider(IIconProvider* icons);
  void SetActionSink(IPluginActionSink* sink);
  void SetEventSink(IPluginEventSink* sink);

  void Discover(const std::string& plugin_dir);

  // 运行时重新扫描插件目录：新出现的 DLL 先校验再登记，校验失败的直接跳过；
  // 之前被拒绝的类型会重试一次（用户可能已经修好并替换了文件）。
  // 已通过校验的类型与正在运行的实例保持不变。
  ReloadResult ReloadPlugins();

  void LoadEnabled();

  void ReleaseDiscoveredModules();

  // 只允许 Source 类型添加实例；Notifier 每种类型仍只有一个 canonical 实例。
  AddInstanceResult AddInstance(const std::string& type_id,
                                const std::string& display_name);
  bool RemoveInstance(const std::string& instance_id,
                      std::string* out_error = nullptr);
  bool RenameInstance(const std::string& instance_id,
                      const std::string& display_name);

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
  std::vector<PluginTypeInfo> SnapshotTypes() const;
  std::vector<NotifierSubscriptions> ActiveNotifierSubscriptions() const;
  Stats                   SnapshotStats() const;
  PluginState             StateOf(const std::string& id) const;
  sm::IPlugin* InstanceOf(const std::string& id) const;
  bool         HasPlugin(const std::string& id) const;

  void BeginShutdown();
  bool shutting_down() const { return shutting_down_.load(); }

  std::size_t main_thread_queue_size() const { return post_queue_.Size(); }

 private:
  struct PluginType;
  struct Record;
  struct PendingDisable {
    std::string id;
    std::string reason;
    bool        crashed = false;
  };

  struct ValidationResult {
    bool                     ok = false;
    std::string              reason;
    std::vector<const char*> subscriptions;
  };

  PluginType* AddType(const std::string& file_stem, const std::string& path);
  PluginType* FindType(const std::string& type_id) const;
  void TryLoadAndValidateType(PluginType& type);
  ValidationResult Validate(const PluginType& type, const sm::PluginMeta& meta);
  void RejectType(PluginType& type, const std::string& reason);
  bool EnsureTypeLoaded(PluginType& type);
  void UnloadTypeModule(PluginType& type);
  void MaybeUnloadType(PluginType& type);

  void SynchronizeInstances();
  Record* AddRecord(const std::string& id, PluginType& type);
  Record* FindRecord(const std::string& id) const;
  std::string GenerateInstanceId(const PluginType& type) const;
  PluginHostSetup MakeHostSetup(const Record& record) const;

  void CreateAndInitialize(Record& record);
  bool InitializeInstance(Record& record);
  void SetState(Record& record, PluginState state, const char* reason,
                bool crashed = false);
  void CallStop(Record& record);
  void CallExit(Record& record);
  void DestroyInstance(Record& record);
  bool FinishTeardown(Record& record);  // 崩溃自动禁用的收尾
  bool TryFinishRemoval(Record& record);  // 用户删除实例的收尾（成功时记录已被移除）
  void EraseRecord(Record& record);

  void EndCallFailed(const std::string& id, const char* method, const guard::Outcome& outcome);

  // 调度线程不能直接改 ConfigStore/Router/UI：禁用请求先入队，由主线程执行。
  void ApplyPendingDisables();

  config::ConfigStore& config_;
  IScheduleSink*       scheduler_ = nullptr;
  IUiSession*          ui_ = nullptr;
  IIconProvider*       icons_ = nullptr;
  IPluginActionSink*   actions_ = nullptr;
  IPluginEventSink*    events_ = nullptr;

  std::string                              plugin_dir_;
  std::vector<std::unique_ptr<PluginType>> types_;
  std::map<std::string, PluginType*>       type_index_;
  std::vector<std::unique_ptr<Record>>     records_;
  std::map<std::string, Record*>           index_;

  // 只保护 index_/type_index_ 这两张表的查找与增删：调度线程的 RunStart 会
  // 查 index_，主线程可能同时在增删记录。记录对象本身由 IsExecuting 保证
  // 在执行期间不被销毁，因此查表返回的裸指针可以安全使用。
  mutable std::mutex index_mutex_;
  const std::thread::id main_thread_id_;

  std::mutex                 pending_disable_mutex_;
  std::vector<PendingDisable> pending_disables_;

  Arena          main_arena_;
  Arena          sched_arena_;
  mutable MainThreadQueue post_queue_;

  std::atomic<bool> shutting_down_{false};
  bool              discovered_ = false;
};

}  // namespace host
}  // namespace sm

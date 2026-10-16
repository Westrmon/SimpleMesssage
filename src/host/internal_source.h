// SimpleMessage — 宿主自身的通知源 sm.host
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "sm/notification.h"

#include "host/config.h"
#include "host/plugin_host.h"
#include "host/plugin_manager.h"

namespace sm {
namespace log {
struct Entry;
}  // namespace log

namespace host {

inline constexpr const char* kInternalSourceId = "sm.host";

namespace host_topic {
inline constexpr const char* kLog = "sm.host.log";
inline constexpr const char* kPlugin = "sm.host.plugin";
inline constexpr const char* kConfig = "sm.host.config";
inline constexpr const char* kLifecycle = "sm.host.lifecycle";
}  // namespace host_topic

enum class HostEvent : std::int32_t {
  PluginRejected = 0,
  PluginInitializeFailed = 1,
  PluginCrashed = 2,
  PluginDisabled = 3,
  ConfigReset = 4,
  ConfigSchemaTooNew = 5,
  ConfigWriteFailed = 6,
  HostStarted = 7,
  HostExited = 8,
  UiFontFallback = 9,
};

class InternalSource final : public IPluginEventSink {
 public:
  InternalSource();
  ~InternalSource() override;
  InternalSource(const InternalSource&) = delete;
  InternalSource& operator=(const InternalSource&) = delete;

  void SetSink(INotificationSink* sink);
  void SetNotifyLogLevel(config::NotifyLogLevel level);

  void PostEvent(HostEvent event, const std::string& subject, const std::string& detail);

  void Tick(std::int64_t now_ms);

  struct RateLimit {
    std::int64_t window_ms = 10 * 1000;
    int          max_per_window = 5;
  };
  void SetRateLimit(const RateLimit& limit);

  static PluginInfo Info();
  static bool IsInternalId(const std::string& plugin_id);

  struct Stats {
    std::int64_t log_notifications = 0;
    std::int64_t log_merged = 0;
    std::int64_t structural = 0;
    std::int64_t suppressed_threshold = 0;
  };
  Stats SnapshotStats() const;
  std::int64_t suppressed_recursion() const {
    return suppressed_recursion_.load(std::memory_order_relaxed);
  }

  void OnPluginStateChanged(const std::string& plugin_id, PluginState state,
                            const char* reason, bool crashed) override;

 private:
  static void OnLogRecord(void* user, const sm::log::Entry& entry);
  void        Bridge(const sm::log::Entry& entry);

  void Emit(const char* topic, sm::Level level, const std::string& title,
            const std::string& content, const char* id);

  struct MergeSummary {
    int         count = 0;
    sm::Level   level = sm::Level::Warning;
    std::string last_message;
  };
  bool ApplyRateLimit(std::int64_t now_ms, const std::string& message, sm::Level level,
                 MergeSummary* out_merge);
  bool TakeExpiredMerge(std::int64_t now_ms, MergeSummary* out_merge);
  void EmitMerge(const MergeSummary& summary);

  INotificationSink*     sink_ = nullptr;
  config::NotifyLogLevel notify_log_level_ = config::NotifyLogLevel::Error;

  mutable std::mutex mutex_;
  RateLimit          rate_limit_;
  std::int64_t       window_start_ms_ = 0;
  int                window_count_ = 0;
  int                merged_count_ = 0;
  sm::Level          merged_max_level_ = sm::Level::Info;
  std::string        last_merged_message_;
  Stats              stats_;

  std::atomic<std::int64_t> suppressed_recursion_{0};
};

}  // namespace host
}  // namespace sm

// SimpleMessage — 内建通知源 sm.host 的实现
#include "host/internal_source.h"

#include <cstdio>

#include "platform/platform.h"
#include "sm/version.h"

#include "host/log.h"
#include "host/notify_scope.h"
#include "host/strings.h"
#include "host/util.h"

namespace sm {
namespace host {
namespace {

struct EventMeta {
  HostEvent   event;
  const char* topic;
  sm::Level   level;
  const char* title;
};

const EventMeta kEventTable[] = {
    {HostEvent::PluginRejected, host_topic::kPlugin, sm::Level::Warning,
     "插件被拒绝"},
    {HostEvent::PluginInitializeFailed, host_topic::kPlugin, sm::Level::Warning,
     "插件初始化失败"},
    {HostEvent::PluginCrashed, host_topic::kPlugin, sm::Level::Critical, "插件崩溃"},
    {HostEvent::PluginDisabled, host_topic::kPlugin, sm::Level::Warning, "插件被禁用"},
    {HostEvent::ConfigReset, host_topic::kConfig, sm::Level::Error, "配置已重置"},
    {HostEvent::ConfigSchemaTooNew, host_topic::kConfig, sm::Level::Critical,
     "配置版本过高"},
    {HostEvent::ConfigWriteFailed, host_topic::kConfig, sm::Level::Error,
     "配置写入失败"},
    {HostEvent::HostStarted, host_topic::kLifecycle, sm::Level::Info, "宿主已启动"},
    {HostEvent::HostExited, host_topic::kLifecycle, sm::Level::Info, "宿主已退出"},
    {HostEvent::UiFontFallback, host_topic::kLog, sm::Level::Error, "界面字体回退"},
};

const EventMeta& MetaOf(HostEvent event) {
  for (const EventMeta& meta : kEventTable) {
    if (meta.event == event) return meta;
  }
  return kEventTable[0];
}

bool MapLevel(sm::LogLevel from, sm::Level* out) {
  // Trace / Debug 不桥接：通知管线与调度器的过程性日志只写这两级，否则会自我触发。
  switch (from) {
    case sm::LogLevel::Error: *out = sm::Level::Error; return true;
    case sm::LogLevel::Warn:  *out = sm::Level::Warning; return true;
    case sm::LogLevel::Info:  *out = sm::Level::Info; return true;
    default: return false;
  }
}

bool PassesThreshold(config::NotifyLogLevel threshold, sm::Level level) {
  switch (threshold) {
    case config::NotifyLogLevel::Off:
      return false;
    case config::NotifyLogLevel::Error:
      return static_cast<int>(level) >= static_cast<int>(sm::Level::Error);
    case config::NotifyLogLevel::Warn:
      return static_cast<int>(level) >= static_cast<int>(sm::Level::Warning);
    case config::NotifyLogLevel::Info:
      return true;
  }
  return false;
}

}  // namespace

InternalSource::InternalSource() {
  window_start_ms_ = platform::monotonic_ms();
  log::SetObserver(&InternalSource::OnLogRecord, this);
}

InternalSource::~InternalSource() {
  log::SetObserver(nullptr, nullptr);
}

void InternalSource::SetSink(INotificationSink* sink) { sink_ = sink; }

void InternalSource::SetNotifyLogLevel(config::NotifyLogLevel level) {
  notify_log_level_ = level;
}

void InternalSource::SetRateLimit(const RateLimit& limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  rate_limit_ = limit;
}

void InternalSource::PostEvent(HostEvent event, const std::string& subject,
                               const std::string& detail) {
  const EventMeta& meta = MetaOf(event);

  std::string title = meta.title;
  if (!subject.empty()) {
    title += " · ";
    title += subject;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.structural;
  }
  Emit(meta.topic, meta.level, title, detail, nullptr);
}

void InternalSource::Tick(std::int64_t now_ms) {
  MergeSummary summary;
  if (!TakeExpiredMerge(now_ms, &summary)) return;
  EmitMerge(summary);
}

void InternalSource::OnLogRecord(void* user, const sm::log::Entry& entry) {
  auto* self = static_cast<InternalSource*>(user);
  if (self != nullptr) self->Bridge(entry);
}

void InternalSource::Bridge(const sm::log::Entry& entry) {
  if (InNotifyScope()) {
    suppressed_recursion_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  sm::Level level = sm::Level::Info;
  if (!MapLevel(entry.level, &level)) return;

  if (!PassesThreshold(notify_log_level_, level)) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.suppressed_threshold;
    return;
  }

  const std::string message(entry.message);
  MergeSummary merge;
  const bool deliver = ApplyRateLimit(platform::monotonic_ms(), message, level, &merge);
  if (merge.count > 0) EmitMerge(merge);
  if (!deliver) return;

  std::string title = strings::LevelText(level);
  title += " · ";
  title += entry.source;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.log_notifications;
  }
  Emit(host_topic::kLog, level, title, message, nullptr);
}

bool InternalSource::ApplyRateLimit(std::int64_t now_ms, const std::string& message,
                               sm::Level level, MergeSummary* out_merge) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (now_ms - window_start_ms_ >= rate_limit_.window_ms) {
    if (merged_count_ > 0) {
      out_merge->count = merged_count_;
      out_merge->level = merged_max_level_;
      out_merge->last_message = last_merged_message_;
      merged_count_ = 0;
      merged_max_level_ = sm::Level::Debug;
    }
    window_start_ms_ = now_ms;
    window_count_ = 0;
  }

  ++window_count_;
  if (window_count_ <= rate_limit_.max_per_window) return true;

  ++merged_count_;
  ++stats_.log_merged;
  last_merged_message_ = message;
  if (static_cast<int>(level) > static_cast<int>(merged_max_level_)) {
    merged_max_level_ = level;
  }
  return false;
}

bool InternalSource::TakeExpiredMerge(std::int64_t now_ms, MergeSummary* out_merge) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (merged_count_ == 0) return false;
  if (now_ms - window_start_ms_ < rate_limit_.window_ms) return false;

  out_merge->count = merged_count_;
  out_merge->level = merged_max_level_;
  out_merge->last_message = last_merged_message_;
  merged_count_ = 0;
  merged_max_level_ = sm::Level::Debug;
  window_start_ms_ = now_ms;
  window_count_ = 0;
  return true;
}

void InternalSource::EmitMerge(const MergeSummary& summary) {
  char title[64];
  std::snprintf(title, sizeof(title), "还有 %d 条类似日志", summary.count);

  std::string content = "（同类日志已合并）最近一条：";
  content += summary.last_message;

  Emit(host_topic::kLog, summary.level, title, content, "sm.host.log.merged");
}

void InternalSource::Emit(const char* topic, sm::Level level, const std::string& title,
                          const std::string& content, const char* id) {
  if (sink_ == nullptr) return;

  sm::Notification n;
  n.title = title.c_str();
  n.content = content.c_str();
  n.topic = topic;
  n.level = level;
  n.time_unix_ms = util::now_unix_ms();
  n.id = id;
  n.summary = nullptr;
  n.actions = sm::Span<sm::Action>{};
  n.source_plugin = nullptr;

  NotifyScope scope;
  sink_->OnNotifications(kInternalSourceId, sm::Span<const sm::Notification>(&n, 1));
}

void InternalSource::OnPluginStateChanged(const std::string& plugin_id, PluginState state,
                                          const char* reason, bool crashed) {
  const std::string detail = (reason != nullptr && *reason != '\0') ? reason
                                                                    : "(未说明原因)";
  switch (state) {
    case PluginState::Rejected:
      PostEvent(HostEvent::PluginRejected, plugin_id, detail);
      break;
    case PluginState::InitializeFailed:
      PostEvent(HostEvent::PluginInitializeFailed, plugin_id, detail);
      break;
    case PluginState::Disabled:
      PostEvent(crashed ? HostEvent::PluginCrashed : HostEvent::PluginDisabled,
                plugin_id, detail);
      break;
    default:
      break;
  }
}

PluginInfo InternalSource::Info() {
  PluginInfo info;
  info.id = kInternalSourceId;
  info.name = "宿主";
  info.version = SM_VERSION_STRING;
  info.description = "宿主自身的事件源：日志、插件、配置与启停";
  info.path = "(内建)";
  info.kind = sm::PluginKind::Source;
  info.state = PluginState::Running;
  info.enabled = true;
  return info;
}

bool InternalSource::IsInternalId(const std::string& plugin_id) {
  return plugin_id == kInternalSourceId;
}

InternalSource::Stats InternalSource::SnapshotStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

}  // namespace host
}  // namespace sm

// SimpleMessage — 通知管线的实现
#include "host/notification_pipeline.h"

#include <algorithm>
#include <vector>

#include "platform/platform.h"
#include "sm/summary.h"

#include "host/log.h"
#include "host/notify_scope.h"

namespace sm {
namespace host {
namespace {

constexpr std::size_t kSummaryBufferBytes = 128;

bool IsContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

bool TruncateUtf8(std::string* text, std::size_t max_bytes) {
  if (text == nullptr || text->size() <= max_bytes) return false;
  std::size_t cut = max_bytes;
  while (cut > 0 && IsContinuationByte((*text)[cut])) --cut;
  text->resize(cut);
  return true;
}

std::string CopyField(const char* text, std::size_t max_bytes, const char* field,
                      const std::string& source) {
  if (text == nullptr) return {};
  std::string out(text);
  if (TruncateUtf8(&out, max_bytes)) {
    log::Writef(sm::LogLevel::Warn, source.c_str(),
                "通知字段 %s 超过 %zu 字节，已截断", field, max_bytes);
  }
  return out;
}

std::string NormalizeSummary(const std::string& text) {
  if (text.empty()) return text;
  std::vector<char> buffer(text.size() + 4);
  sm::make_summary(text.c_str(), text.size(), buffer.data(), buffer.size());
  return std::string(buffer.data());
}

}  // namespace

void OwnedNotification::RebuildView() {
  action_views.clear();
  action_views.reserve(actions.size());
  for (const OwnedAction& action : actions) {
    sm::Action out;
    out.id = action.id.c_str();
    out.label = action.label.empty() ? nullptr : action.label.c_str();
    action_views.push_back(out);
  }

  view.title = title.c_str();
  view.content = content.c_str();
  view.summary = summary.c_str();
  view.topic = topic.c_str();
  view.level = level;
  view.time_unix_ms = time_unix_ms;
  view.id = id.empty() ? nullptr : id.c_str();
  view.url = url.empty() ? nullptr : url.c_str();
  view.actions = sm::Span<sm::Action>(action_views.data(), action_views.size());
  view.source_plugin = source_plugin.c_str();
  view.seq = seq;
}

NotificationPipeline::NotificationPipeline(PipelineLimits limits) : limits_(limits) {
  last_drop_report_ms_ = -limits_.drop_report_interval_ms;
}

NotificationPipeline::~NotificationPipeline() = default;

void NotificationPipeline::SetRouter(const Router* router) { router_ = router; }

void NotificationPipeline::SetDelivery(INotifierDelivery* delivery) {
  delivery_ = delivery;
}

void NotificationPipeline::OnNotifications(
    const std::string& source_plugin_id,
    sm::Span<const sm::Notification> items) {
  if (items.data == nullptr) return;
  for (std::size_t i = 0; i < items.size; ++i) {
    Admit(source_plugin_id, items.data[i]);
  }
}

bool NotificationPipeline::Admit(const std::string& source_plugin,
                                 const sm::Notification& src) {

  OwnedNotification owned;
  owned.title = CopyField(src.title, sm::limits::kMaxTitleBytes, "title", source_plugin);
  owned.content =
      CopyField(src.content, sm::limits::kMaxContentBytes, "content", source_plugin);
  owned.topic = CopyField(src.topic, sm::limits::kMaxTopicBytes, "topic", source_plugin);
  owned.id = CopyField(src.id, sm::limits::kMaxIdBytes, "id", source_plugin);
  owned.url = CopyField(src.url, sm::limits::kMaxUrlBytes, "url", source_plugin);
  owned.level = src.level;
  owned.time_unix_ms = src.time_unix_ms;

  if (src.actions.data != nullptr) {
    const std::size_t count = src.actions.size;
    const std::size_t keep = std::min(count, sm::limits::kMaxActionsPerNotification);
    owned.actions.reserve(keep);
    for (std::size_t i = 0; i < keep; ++i) {
      OwnedAction action;
      action.id = CopyField(src.actions.data[i].id, sm::limits::kMaxActionIdBytes,
                            "action.id", source_plugin);
      action.label = CopyField(src.actions.data[i].label,
                               sm::limits::kMaxActionLabelBytes, "action.label",
                               source_plugin);
      owned.actions.push_back(std::move(action));
    }
    if (count > keep) {
      log::Writef(sm::LogLevel::Warn, source_plugin.c_str(),
                  "通知的动作过多（%zu 个，上限 %zu），已丢弃尾部 %zu 个", count,
                  sm::limits::kMaxActionsPerNotification, count - keep);
    }
  }

  if (owned.title.empty() || owned.content.empty() || owned.topic.empty()) {
    const char* missing = !owned.title.empty()
                              ? (!owned.content.empty() ? "topic" : "content")
                              : "title";
    log::Writef(sm::LogLevel::Warn, source_plugin.c_str(),
                "通知缺少必填项 %s，已丢弃", missing);
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.rejected;
    return false;
  }

  if (src.summary != nullptr && src.summary[0] != '\0') {
    owned.summary = NormalizeSummary(
        CopyField(src.summary, sm::limits::kMaxContentBytes, "summary", source_plugin));
  } else {
    char buffer[kSummaryBufferBytes];
    sm::make_summary(owned.content.c_str(), sm::limits::kSummaryCodepoints, buffer,
                     sizeof(buffer));
    owned.summary = buffer;
  }

  owned.source_plugin = source_plugin;

  std::lock_guard<std::mutex> lock(mutex_);

  if (!owned.id.empty()) {
    // 同 id 的通知是原地更新，必须保住原来的 seq 与已读状态 —— 换一条新的会让
    // 浮层以为来了新通知、让已读记录凭空回滚。
    const auto it = by_id_.find(owned.id);
    if (it != by_id_.end()) {
      OwnedNotification& existing = *it->second;
      const std::int64_t keep_seq = existing.seq;
      const bool keep_read = existing.read;
      existing = std::move(owned);
      existing.seq = keep_seq;
      existing.read = keep_read;
      existing.RebuildView();
      ++stats_.produced;
      ++stats_.updated;
      return true;
    }
  }

  owned.seq = next_seq_++;
  while (!queue_.empty() && queue_.size() >= limits_.queue_capacity) {
    DropOldestLocked();
  }
  queue_.push_back(std::move(owned));
  OwnedNotification& stored = queue_.back();
  stored.RebuildView();
  if (!stored.id.empty()) by_id_[stored.id] = &stored;
  ++stats_.produced;
  return true;
}

void NotificationPipeline::DropOldestLocked() {
  OwnedNotification& oldest = queue_.front();
  if (!oldest.id.empty()) by_id_.erase(oldest.id);
  queue_.pop_front();
  ++stats_.dropped;
  ++dropped_since_report_;
  ReportDropsLocked();
}

void NotificationPipeline::ReportDropsLocked() {
  if (dropped_since_report_ == 0) return;
  const std::int64_t now = platform::monotonic_ms();
  if (now - last_drop_report_ms_ < limits_.drop_report_interval_ms) return;

  log::Writef(sm::LogLevel::Warn, "host", "通知队列已满，已丢弃 %lld 条最旧的通知",
              static_cast<long long>(dropped_since_report_));
  dropped_since_report_ = 0;
  last_drop_report_ms_ = now;
}

std::size_t NotificationPipeline::PumpMainThread(std::size_t max_items) {
  NotifyScope notify_scope;

  const std::size_t budget =
      (max_items == 0) ? limits_.per_frame_budget : max_items;
  const Router* const router = router_;
  INotifierDelivery* const delivery = delivery_;

  std::vector<std::string> matched;
  std::size_t consumed = 0;

  for (std::size_t i = 0; i < budget; ++i) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) break;
      OwnedNotification& front = queue_.front();
      if (!front.id.empty()) by_id_.erase(front.id);
      TrimHistoryLocked();
      history_.push_back(std::move(front));
      queue_.pop_front();
      history_.back().RebuildView();
    }

    const OwnedNotification& n = history_.back();
    ++consumed;

    matched.clear();
    if (router != nullptr) router->Match(n.topic, &matched);

    bool delivered = false;
    if (delivery != nullptr) {
      for (const std::string& id : matched) {
        if (delivery->Deliver(id, n.view)) delivered = true;
      }
    }

    if (!delivered) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.undelivered;
      }
      if (matched.empty()) {
        log::Writef(sm::LogLevel::Debug, "host",
                    "通知 #%lld（topic=%s）没有 Notifier 接收，仅进历史",
                    static_cast<long long>(n.seq), n.topic.c_str());
      } else if (delivery == nullptr && !warned_no_delivery_) {
        warned_no_delivery_ = true;
        log::Write(sm::LogLevel::Warn, "host",
                   "通知投递接口未接线：路由命中的通知不会被任何 Notifier 收到");
      }
    }
  }
  return consumed;
}

void NotificationPipeline::TrimHistoryLocked() {
  while (!history_.empty() && history_.size() >= limits_.history_capacity) {
    history_.pop_front();
  }
}

void NotificationPipeline::SetHistoryCapacity(std::size_t capacity) {
  capacity = std::max<std::size_t>(1, capacity);
  capacity = std::min(capacity, sm::limits::kNotificationHistoryCapacity);
  std::lock_guard<std::mutex> lock(mutex_);
  limits_.history_capacity = capacity;
  while (history_.size() > capacity) history_.pop_front();
}

void NotificationPipeline::ClearHistory() {
  history_.clear();
}

NotificationPipeline::Stats NotificationPipeline::SnapshotStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

std::size_t NotificationPipeline::queued() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

std::size_t NotificationPipeline::history_size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return history_.size();
}

std::vector<const OwnedNotification*> NotificationPipeline::History() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<const OwnedNotification*> out;
  out.reserve(history_.size());
  for (const OwnedNotification& n : history_) out.push_back(&n);
  return out;
}

const OwnedNotification* NotificationPipeline::FindBySeq(std::int64_t seq) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const OwnedNotification& n : history_) {
    if (n.seq == seq) return &n;
  }
  return nullptr;
}

void NotificationPipeline::MarkRead(std::int64_t seq) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (OwnedNotification& n : history_) {
    if (n.seq == seq) {
      n.read = true;
      return;
    }
  }
}

void NotificationPipeline::MarkAllRead() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (OwnedNotification& n : history_) n.read = true;
}

}  // namespace host
}  // namespace sm

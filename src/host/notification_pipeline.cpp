// SimpleMessage — 通知管线的实现
#include "host/notification_pipeline.h"

#include <algorithm>
#include <vector>

#include "platform/platform.h"
#include "sm/summary.h"

#include "host/log.h"
#include "host/notify_scope.h"
#include "host/rule_save.h"
#include "host/util.h"

namespace sm {
namespace host {
namespace {

std::string CopyField(const char* text, std::size_t max_bytes, const char* field,
                      const std::string& source) {
  if (text == nullptr) return {};
  std::string out(text);
  if (util::TruncateUtf8(&out, max_bytes)) {
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

void LogRuleWarnings(const char* source, const RuleDiagnostics& warnings) {
  for (const RuleDiagnostic& warning : warnings) {
    if (warning.severity != RuleDiagnostic::Severity::Warn) continue;
    log::Writef(sm::LogLevel::Warn, source, "规则：%s", warning.message.c_str());
  }
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
  view.icon = icon.empty() ? nullptr : icon.c_str();
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

void NotificationPipeline::SetRuleSaveSink(RuleSaveSink* sink) {
  rule_save_ = sink;
}

void NotificationPipeline::SetRuleSaveEnabled(bool enabled) {
  rule_save_enabled_.store(enabled, std::memory_order_relaxed);
}

void NotificationPipeline::SaveRuleHit(const RuleSet& rules,
                                       const OwnedNotification& n,
                                       const RuleSaveHit& hit,
                                       bool delivery_phase,
                                       const std::string& notifier_id) {
  RuleSaveSink* const sink = rule_save_;
  if (sink == nullptr ||
      !rule_save_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  if (hit.rule_index >= rules.size()) return;
  const Rule& rule = rules.rules()[hit.rule_index];
  if (!rule.save.enabled) return;

  NotifyScope notify_scope;
  sm::Notification view = n.view;
  view.title = hit.title.c_str();
  view.content = hit.content.c_str();
  view.summary = hit.summary.c_str();
  view.level = hit.level;
  sink->Save(rule, view, delivery_phase, notifier_id);
}

void NotificationPipeline::SetRules(std::shared_ptr<RuleSet> rules) {
  std::lock_guard<std::mutex> lock(rules_mutex_);
  rules_ = std::move(rules);
}

std::shared_ptr<RuleSet> NotificationPipeline::RulesSnapshot() const {
  std::lock_guard<std::mutex> lock(rules_mutex_);
  return rules_;
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
  owned.icon = CopyField(src.icon, sm::limits::kMaxIconBytes, "icon", source_plugin);
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

  // 来源提供的简介先折叠空白；没有提供就留空，交给来源趟的 auto_summary 规则。
  if (src.summary != nullptr && src.summary[0] != '\0') {
    owned.summary = NormalizeSummary(
        CopyField(src.summary, sm::limits::kMaxContentBytes, "summary", source_plugin));
  }

  owned.source_plugin = source_plugin;

  // ---- 来源趟规则：全局改写/过滤，结果会进入历史 ----
  const bool saving = rule_save_ != nullptr &&
                      rule_save_enabled_.load(std::memory_order_relaxed);
  const std::shared_ptr<RuleSet> rules = RulesSnapshot();
  if (rules != nullptr && !rules->empty()) {
    RuleFields fields(owned.title, owned.content, owned.summary, owned.topic,
                      owned.level);
    RuleDiagnostics warnings;
    RuleSaveHits save_hits;
    const bool keep = rules->ApplySource(&fields, source_plugin, &warnings,
                                         saving ? &save_hits : nullptr);
    LogRuleWarnings(source_plugin.c_str(), warnings);

    // 先落盘，再处理 drop/校验失败：命中即保存，不因通知被丢弃而丢失。
    if (saving && !save_hits.empty()) {
      owned.RebuildView();
      for (const RuleSaveHit& hit : save_hits) {
        SaveRuleHit(*rules, owned, hit, false, std::string());
      }
    }

    if (!keep) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.filtered_source;
      return false;
    }

    if (fields.has_title()) {
      owned.title.assign(fields.title_buffer());
      if (util::TruncateUtf8(&owned.title, sm::limits::kMaxTitleBytes)) {
        log::Writef(sm::LogLevel::Warn, source_plugin.c_str(),
                    "规则改写后的 title 超过 %zu 字节，已截断",
                    sm::limits::kMaxTitleBytes);
      }
    }
    if (fields.has_content()) {
      owned.content.assign(fields.content_buffer());
      if (util::TruncateUtf8(&owned.content, sm::limits::kMaxContentBytes)) {
        log::Writef(sm::LogLevel::Warn, source_plugin.c_str(),
                    "规则改写后的 content 超过 %zu 字节，已截断",
                    sm::limits::kMaxContentBytes);
      }
    }
    if (fields.has_summary()) {
      owned.summary.assign(fields.summary_buffer());
      if (util::TruncateUtf8(&owned.summary, sm::limits::kMaxContentBytes)) {
        log::Writef(sm::LogLevel::Warn, source_plugin.c_str(),
                    "规则改写后的 summary 超过 %zu 字节，已截断",
                    sm::limits::kMaxContentBytes);
      }
      owned.summary = NormalizeSummary(owned.summary);
    }
    if (fields.has_level()) owned.level = fields.level();
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

  std::lock_guard<std::mutex> lock(mutex_);

  if (!owned.id.empty()) {
    // 同一来源实例的同 id 通知是原地更新，必须保住原来的 seq 与已读状态 ——
    // 换一条新的会让浮层以为来了新通知、让已读记录凭空回滚。
    const auto it = by_id_.find({owned.source_plugin, owned.id});
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
  if (!stored.id.empty()) by_id_[{stored.source_plugin, stored.id}] = &stored;
  ++stats_.produced;
  return true;
}

void NotificationPipeline::DropOldestLocked() {
  OwnedNotification& oldest = queue_.front();
  if (!oldest.id.empty()) by_id_.erase({oldest.source_plugin, oldest.id});
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
  const std::shared_ptr<RuleSet> rules = RulesSnapshot();
  const bool has_delivery_rules =
      rules != nullptr && rules->has_delivery_rules();
  const bool saving = rule_save_ != nullptr &&
                      rule_save_enabled_.load(std::memory_order_relaxed);
  std::vector<unsigned char> saved;

  std::vector<std::string> matched;
  std::size_t consumed = 0;

  for (std::size_t i = 0; i < budget; ++i) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) break;
      OwnedNotification& front = queue_.front();
      if (!front.id.empty()) by_id_.erase({front.source_plugin, front.id});
      TrimHistoryLocked();
      history_.push_back(std::move(front));
      queue_.pop_front();
      history_.back().RebuildView();
    }

    const OwnedNotification& n = history_.back();
    ++consumed;

    if (saving && rules != nullptr) saved.assign(rules->size(), 0);
    matched.clear();
    if (router != nullptr) router->Match(n.topic, &matched);

    bool delivered = false;
    int filtered = 0;
    if (delivery != nullptr) {
      for (const std::string& id : matched) {
        // ---- 投递趟规则：只影响本次 Deliver 的视图，历史保持原样 ----
        if (has_delivery_rules) {
          RuleFields fields(n.title, n.content, n.summary, n.topic, n.level);
          RuleDiagnostics warnings;
          RuleSaveHits save_hits;
          const bool keep = rules->ApplyDelivery(
              &fields, n.source_plugin, id, &warnings,
              saving ? &save_hits : nullptr);
          LogRuleWarnings("host", warnings);

          // 同一规则对同一条通知只保存一次：取第一个命中的 Notifier，
          // 避免宽泛的投递规则为每个 Notifier 重复追写。
          for (const RuleSaveHit& hit : save_hits) {
            if (hit.rule_index < saved.size() && saved[hit.rule_index] == 0) {
              saved[hit.rule_index] = 1;
              SaveRuleHit(*rules, n, hit, true, id);
            }
          }

          if (!keep) {
            ++filtered;
            continue;
          }
          if (fields.changed()) {
            std::string title(fields.title());
            std::string content(fields.content());
            std::string summary(fields.summary());
            sm::Notification view = n.view;
            view.title = title.c_str();
            view.content = content.c_str();
            view.summary = summary.c_str();
            view.level = fields.level();
            if (delivery->Deliver(id, view)) delivered = true;
            continue;
          }
        }
        if (delivery->Deliver(id, n.view)) delivered = true;
      }
    }

    if (filtered != 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      stats_.filtered_delivery += filtered;
    }

    if (!delivered) {
      if (filtered != 0) {
        log::Writef(sm::LogLevel::Debug, "host",
                    "通知 #%lld 被投递规则拦截（%d 个 Notifier），仍保留在历史",
                    static_cast<long long>(n.seq), filtered);
      } else {
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
  std::lock_guard<std::mutex> lock(mutex_);
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

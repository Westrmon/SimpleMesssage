// SimpleMessage — 通知管线：入队、清洗、去重、背压、历史、路由投递
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "sm/notification.h"
#include "sm/types.h"

#include "host/plugin_host.h"
#include "host/router.h"
#include "host/rules.h"

namespace sm {
namespace host {

class RuleSaveSink;

struct PipelineLimits {
  std::size_t  queue_capacity = sm::limits::kNotificationQueueCapacity;
  std::size_t  history_capacity = sm::limits::kNotificationHistoryCapacity;
  std::size_t  per_frame_budget = 64;
  std::int64_t drop_report_interval_ms = 10 * 1000;
};

struct OwnedAction {
  std::string id;
  std::string label;
};

struct OwnedNotification {

  std::string              title;
  std::string              content;
  std::string              summary;
  std::string              topic;
  std::string              id;
  std::string              url;
  std::string              icon;
  std::string              source_plugin;
  std::vector<OwnedAction> actions;
  sm::Level                level = sm::Level::Info;
  std::int32_t             importance = 0;
  std::int64_t             time_unix_ms = 0;
  std::int64_t             seq = 0;
  bool                     read = false;

  sm::Notification        view{};
  std::vector<sm::Action> action_views;

  void RebuildView();
};

class NotificationPipeline final : public INotificationSink {
 public:
  explicit NotificationPipeline(PipelineLimits limits = PipelineLimits());
  ~NotificationPipeline() override;
  NotificationPipeline(const NotificationPipeline&) = delete;
  NotificationPipeline& operator=(const NotificationPipeline&) = delete;

  void SetRouter(const Router* router);
  void SetDelivery(INotifierDelivery* delivery);

  // sink 的生命周期必须覆盖管线；开关可以在运行中切换（UI 线程写，
  // 来源线程与主线程读）。
  void SetRuleSaveSink(RuleSaveSink* sink);
  void SetRuleSaveEnabled(bool enabled);

  // 规则集可以在运行中替换：来源线程的 Admit 与主线程的 Pump 都只抓一份
  // snapshot（shared_ptr），因此重载不会和正在处理的通知竞争。
  void SetRules(std::shared_ptr<RuleSet> rules);
  std::shared_ptr<RuleSet> RulesSnapshot() const;

  void OnNotifications(const std::string& source_plugin_id,
                       sm::Span<const sm::Notification> items) override;

  std::size_t PumpMainThread(std::size_t max_items = 0);

  struct Stats {
    std::int64_t produced = 0;
    std::int64_t updated = 0;
    std::int64_t rejected = 0;
    std::int64_t dropped = 0;
    std::int64_t undelivered = 0;
    // 被规则过滤：来源趟（通知根本没进历史）与投递趟（该 Notifier 收不到）。
    std::int64_t filtered_source = 0;
    std::int64_t filtered_delivery = 0;
  };

  Stats       SnapshotStats() const;
  std::size_t queued() const;
  std::size_t history_size() const;

  std::vector<const OwnedNotification*> History() const;
  const OwnedNotification*              FindBySeq(std::int64_t seq) const;

  void SetHistoryCapacity(std::size_t capacity);

  void ClearHistory();

  void MarkRead(std::int64_t seq);
  void MarkAllRead();

  const PipelineLimits& limits() const { return limits_; }

 private:
  bool Admit(const std::string& source_plugin, const sm::Notification& src);

  // 用命中快照覆盖视图字段后交给 sink；调用前需确认开关已打开。
  void SaveRuleHit(const RuleSet& rules, const OwnedNotification& n,
                   const RuleSaveHit& hit, bool delivery_phase,
                   const std::string& notifier_id);

  void DropOldestLocked();
  void ReportDropsLocked();
  void TrimHistoryLocked();

  mutable std::mutex mutex_;
  mutable std::mutex rules_mutex_;
  std::shared_ptr<RuleSet> rules_;
  std::deque<OwnedNotification> queue_;
  std::deque<OwnedNotification> history_;
  // 键是（来源实例 id, 通知 id）：多个实例可能上报相同的通知 id（例如两个
  // Gotify 应用的消息号），不能互相覆盖。
  std::map<std::pair<std::string, std::string>, OwnedNotification*> by_id_;

  std::int64_t next_seq_ = 1;
  std::int64_t dropped_since_report_ = 0;
  std::int64_t last_drop_report_ms_ = 0;
  Stats        stats_;

  PipelineLimits    limits_;
  const Router*     router_ = nullptr;
  INotifierDelivery* delivery_ = nullptr;
  RuleSaveSink*     rule_save_ = nullptr;
  std::atomic<bool> rule_save_enabled_{false};
  bool              warned_no_delivery_ = false;
};

}  // namespace host
}  // namespace sm

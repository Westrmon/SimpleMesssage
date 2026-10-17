// SimpleMessage — 通知管线：入队、清洗、去重、背压、历史、路由投递
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "sm/notification.h"
#include "sm/types.h"

#include "host/plugin_host.h"
#include "host/router.h"

namespace sm {
namespace host {

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

  void OnNotifications(const std::string& source_plugin_id,
                       sm::Span<const sm::Notification> items) override;

  std::size_t PumpMainThread(std::size_t max_items = 0);

  struct Stats {
    std::int64_t produced = 0;
    std::int64_t updated = 0;
    std::int64_t rejected = 0;
    std::int64_t dropped = 0;
    std::int64_t undelivered = 0;
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

  void DropOldestLocked();
  void ReportDropsLocked();
  void TrimHistoryLocked();

  mutable std::mutex mutex_;
  std::deque<OwnedNotification> queue_;
  std::deque<OwnedNotification> history_;
  std::map<std::string, OwnedNotification*> by_id_;

  std::int64_t next_seq_ = 1;
  std::int64_t dropped_since_report_ = 0;
  std::int64_t last_drop_report_ms_ = 0;
  Stats        stats_;

  PipelineLimits    limits_;
  const Router*     router_ = nullptr;
  INotifierDelivery* delivery_ = nullptr;
  bool              warned_no_delivery_ = false;
};

}  // namespace host
}  // namespace sm

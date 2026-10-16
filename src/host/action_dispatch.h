// SimpleMessage — 动作点击的分发
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "sm/notification.h"

#include "host/plugin_host.h"

namespace sm {
namespace host {

class NotificationPipeline;
class Router;

class IActionDispatcher {
 public:
  virtual ~IActionDispatcher() = default;

  virtual bool PostAction(std::int64_t notification_seq, const char* action_id) = 0;
};

class ActionDispatcher final : public IActionDispatcher {
 public:
  static constexpr std::size_t kQueueCapacity = 256;
  static constexpr std::size_t kPerFrameBudget = 64;

  ActionDispatcher() = default;
  ActionDispatcher(const ActionDispatcher&) = delete;
  ActionDispatcher& operator=(const ActionDispatcher&) = delete;

  void SetPipeline(NotificationPipeline* pipeline) { pipeline_ = pipeline; }
  void SetRouter(const Router* router) { router_ = router; }
  void SetRunner(IActionRunner* runner) { runner_ = runner; }

  bool PostAction(std::int64_t notification_seq, const char* action_id) override;

  std::size_t PumpMainThread(std::size_t max_items = 0);

  std::size_t queued() const { return queue_.size(); }

  struct Stats {
    std::int64_t posted = 0;
    std::int64_t dispatched = 0;
    std::int64_t expired = 0;
    std::int64_t unrouted = 0;
    std::int64_t crashed = 0;
    std::int64_t dropped = 0;
  };
  Stats SnapshotStats() const { return stats_; }

 private:
  struct Pending {
    std::int64_t seq = 0;
    std::string  action_id;
  };

  NotificationPipeline* pipeline_ = nullptr;
  const Router*         router_ = nullptr;
  IActionRunner*        runner_ = nullptr;
  std::deque<Pending>   queue_;
  Stats                 stats_;
};

}  // namespace host
}  // namespace sm

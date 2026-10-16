// SimpleMessage — 动作点击分发的实现
#include "host/action_dispatch.h"

#include <vector>

#include "host/log.h"
#include "host/notification_pipeline.h"
#include "host/router.h"

namespace sm {
namespace host {

bool ActionDispatcher::PostAction(std::int64_t notification_seq, const char* action_id) {
  if (action_id == nullptr || action_id[0] == '\0') return false;
  if (queue_.size() >= kQueueCapacity) {
    ++stats_.dropped;
    return false;
  }
  Pending pending;
  pending.seq = notification_seq;
  pending.action_id = action_id;
  queue_.push_back(std::move(pending));
  ++stats_.posted;
  return true;
}

std::size_t ActionDispatcher::PumpMainThread(std::size_t max_items) {
  const std::size_t budget = (max_items == 0) ? kPerFrameBudget : max_items;
  std::size_t dispatched_calls = 0;
  std::vector<std::string> targets;

  for (std::size_t consumed = 0; consumed < budget && !queue_.empty(); ++consumed) {
    Pending pending = std::move(queue_.front());
    queue_.pop_front();

    const OwnedNotification* const n =
        (pipeline_ != nullptr) ? pipeline_->FindBySeq(pending.seq) : nullptr;
    if (n == nullptr) {
      ++stats_.expired;
      continue;
    }

    // 在路由之前就标已读：没有 Notifier 订阅这条 topic 时，点击同样应该算已读。
    pipeline_->MarkRead(pending.seq);
    log::Writef(sm::LogLevel::Debug, "host", "seq=%lld 的动作 [%s] 已分发，该通知标为已读",
                static_cast<long long>(pending.seq), pending.action_id.c_str());

    targets.clear();
    if (router_ != nullptr) router_->Match(n->topic, &targets);
    if (targets.empty()) {
      ++stats_.unrouted;
      continue;
    }

    for (const std::string& id : targets) {
      if (runner_ == nullptr) break;
      if (!runner_->InvokeOnAction(id, n->view, pending.action_id.c_str())) {
        ++stats_.crashed;
      }
      ++stats_.dispatched;
      ++dispatched_calls;
    }
  }
  return dispatched_calls;
}

}  // namespace host
}  // namespace sm

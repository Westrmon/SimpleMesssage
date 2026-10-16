// SimpleMessage — 调度器实现

#include "host/scheduler.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "host/log.h"
#include "host/util.h"

namespace sm {
namespace host {

// 并发铁律：heap_ / slots_ / executing_ 受 mutex_ 保护，插件代码一律在锁外运行；
// 改过堆之后必须 wake()；绝不把 Slot* 带出锁外，只带 plugin id。

namespace {

// 空闲等待上限：忘了 wake() 的后果是"晚最多 1 分钟"，而不是"从此不动"。
constexpr std::int64_t kIdleWaitMs = 60 * 1000;

constexpr std::int64_t kDefaultBackoffBaseMs = 1000;

constexpr std::int64_t kStaleFallbackDelayMs = 1000;

constexpr std::int64_t kDestructorGraceMs = 5 * 1000;

}  // namespace

Scheduler::Scheduler(SchedulerLimits limits) : limits_(limits) {
  wake_waiter_ = platform::create_waiter();
  done_waiter_ = platform::create_waiter();
}

Scheduler::~Scheduler() {
  if (thread_.joinable()) {
    request_stop();
    if (!join(kDestructorGraceMs)) {
      sm::log::Write(sm::LogLevel::Warn, "sched", "进程退出前调度线程仍未结束");
    }
  }

  // abandoned_ 为真说明调度线程还活着：两个 waiter 故意不销毁，用几百字节的泄漏
  // 换"退出途中不踩已释放的事件对象"。
  if (!abandoned_) {
    platform::destroy_waiter(wake_waiter_);
    platform::destroy_waiter(done_waiter_);
  }
}

void Scheduler::SetPluginRunner(IPluginRunner* runner) { runner_ = runner; }
void Scheduler::SetNotificationSink(INotificationSink* sink) { notifications_ = sink; }

void Scheduler::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { Loop(); });
}

void Scheduler::request_stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake();
}

bool Scheduler::join(std::int64_t grace_ms) {
  if (!thread_.joinable()) return true;
  if (grace_ms < 0) grace_ms = 0;

  if (!done_waiter_->wait(grace_ms)) {
    sm::log::WriteCriticalf(sm::LogLevel::Warn, "sched",
                            "调度线程未在 %lld ms 内结束，放弃等待并 detach",
                            static_cast<long long>(grace_ms));
    abandoned_ = true;
    thread_.detach();
    return false;
  }

  thread_.join();
  return true;
}

void Scheduler::wake() {
  if (wake_waiter_ != nullptr) wake_waiter_->signal();
}

void Scheduler::set_paused(bool paused) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
  }
  wake();
}

bool Scheduler::paused() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return paused_;
}

void Scheduler::Add(const std::string& plugin_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot& slot = slots_[plugin_id];
    slot.id = plugin_id;
    slot.failures = 0;
    slot.timeouts = 0;
    slot.base_delay_ms = 0;
    slot.run_delay_ms = 0;
    SetNextDue(slot, platform::monotonic_ms());
  }
  wake();
}

void Scheduler::ScheduleNext(const std::string& plugin_id, std::int64_t delay_ms) {
  // 非正数是"忽略 + 记 Warn"，不是清零：schedule_next(60000) 之后再
  // schedule_next(0)，60 秒那次注册仍然有效。
  if (delay_ms <= 0) {
    sm::log::Writef(sm::LogLevel::Warn, plugin_id.c_str(),
                    "schedule_next(%lld) 不是正数，已忽略",
                    static_cast<long long>(delay_ms));
    return;
  }

  const std::int64_t now = platform::monotonic_ms();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // 已禁用 / Notifier 插件不在 slots_ 里，这里不能凭空建槽位 —— 那等于让禁用的
    // 插件复活。
    auto it = slots_.find(plugin_id);
    if (it == slots_.end()) {
      sm::log::Writef(sm::LogLevel::Debug, plugin_id.c_str(),
                      "收到来自不可调度插件的调度注册，已忽略");
      return;
    }
    it->second.run_delay_ms = delay_ms;
    SetNextDue(it->second, now + delay_ms);
  }
  wake();
}

void Scheduler::ScheduleAt(const std::string& plugin_id, std::int64_t unix_ms) {
  const std::int64_t delay_ms = unix_ms - util::now_unix_ms();
  const std::int64_t now = platform::monotonic_ms();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(plugin_id);
    if (it == slots_.end()) {
      sm::log::Writef(sm::LogLevel::Debug, plugin_id.c_str(),
                      "收到来自不可调度插件的调度注册，已忽略");
      return;
    }
    if (delay_ms > 0) it->second.run_delay_ms = delay_ms;
    SetNextDue(it->second, now + std::max<std::int64_t>(delay_ms, 0));
  }
  wake();
}

void Scheduler::Remove(const std::string& plugin_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(plugin_id);
    if (it == slots_.end()) return;
    if (executing_.active && executing_.plugin_id == plugin_id) {
      executing_.removed = true;
    }
    DropFromHeap(it->second);
    slots_.erase(it);
  }
  wake();
}

bool Scheduler::IsCancelled(const std::string& plugin_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!executing_.active || executing_.plugin_id != plugin_id) return false;
  return executing_.watchdog_fired || executing_.removed;
}

bool Scheduler::IsExecuting(const std::string& plugin_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return executing_.active && executing_.plugin_id == plugin_id;
}

void Scheduler::SetNextDue(Slot& slot, std::int64_t due_monotonic_ms) {
  slot.due_monotonic_ms = due_monotonic_ms;
  if (slot.heap_index == kNotInHeap) {
    heap_.push_back(HeapEntry{due_monotonic_ms, slot.id});
    slot.heap_index = heap_.size() - 1;
    HeapSiftUp(slot.heap_index);
    return;
  }
  heap_[slot.heap_index].due_monotonic_ms = due_monotonic_ms;
  HeapSiftUp(slot.heap_index);
  HeapSiftDown(slot.heap_index);
}

void Scheduler::HeapSwap(std::size_t a, std::size_t b) {
  std::swap(heap_[a], heap_[b]);
  for (const std::size_t index : {a, b}) {
    auto it = slots_.find(heap_[index].plugin_id);
    if (it != slots_.end()) it->second.heap_index = index;
  }
}

void Scheduler::HeapSiftUp(std::size_t index) {
  while (index > 0) {
    const std::size_t parent = (index - 1) / 2;
    if (heap_[parent].due_monotonic_ms <= heap_[index].due_monotonic_ms) break;
    HeapSwap(index, parent);
    index = parent;
  }
}

void Scheduler::HeapSiftDown(std::size_t index) {
  const std::size_t count = heap_.size();
  for (;;) {
    const std::size_t left = index * 2 + 1;
    if (left >= count) break;
    std::size_t smallest = left;
    const std::size_t right = left + 1;
    if (right < count && heap_[right].due_monotonic_ms < heap_[left].due_monotonic_ms) {
      smallest = right;
    }
    if (heap_[index].due_monotonic_ms <= heap_[smallest].due_monotonic_ms) break;
    HeapSwap(index, smallest);
    index = smallest;
  }
}

void Scheduler::DropFromHeap(Slot& slot) {
  if (slot.heap_index == kNotInHeap) return;

  const std::size_t index = slot.heap_index;
  const std::size_t last = heap_.size() - 1;

  HeapSwap(index, last);
  heap_.pop_back();
  slot.heap_index = kNotInHeap;

  if (index != last) {
    const std::string moved_id = heap_[index].plugin_id;
    auto it = slots_.find(moved_id);
    if (it != slots_.end()) {
      HeapSiftUp(index);
      HeapSiftDown(it->second.heap_index);
    }
  }
}

void Scheduler::Loop() {
  util::set_thread_tag("sched");
  sm::log::Write(sm::LogLevel::Info, "sched", "调度线程启动");

  for (;;) {
    std::string  task_id;
    std::int64_t due_monotonic_ms = 0;
    std::int64_t wait_ms = kIdleWaitMs;
    bool         stop = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        stop = true;
      } else if (!paused_ && !heap_.empty()) {
        const std::int64_t now = platform::monotonic_ms();
        if (heap_.front().due_monotonic_ms > now) {
          wait_ms = heap_.front().due_monotonic_ms - now;
        } else {
          task_id = heap_.front().plugin_id;
          due_monotonic_ms = heap_.front().due_monotonic_ms;
          auto it = slots_.find(task_id);
          if (it == slots_.end()) {
            HeapSwap(0, heap_.size() - 1);
            heap_.pop_back();
            if (!heap_.empty()) HeapSiftDown(0);
            task_id.clear();
          } else {
            DropFromHeap(it->second);
          }
        }
      }
    }

    if (stop) break;
    if (task_id.empty()) {
      wake_waiter_->wait(wait_ms);
      continue;
    }

    // 欠下的调度不补跑：超过宽限就跳过，并从"现在"重新排。补跑只会把欠下的十几次
    // 一次性变成十几条重复通知。
    const std::int64_t now = platform::monotonic_ms();
    if (now - due_monotonic_ms > limits_.stale_grace_ms) {
      sm::log::Writef(sm::LogLevel::Debug, task_id.c_str(),
                      "注册的时刻已过去 %.1f 秒（超过宽限 %.1f 秒），跳过本次执行",
                      (now - due_monotonic_ms) / 1000.0, limits_.stale_grace_ms / 1000.0);
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = slots_.find(task_id);
      if (it != slots_.end()) {
        const std::int64_t base = (it->second.base_delay_ms > 0)
                                      ? it->second.base_delay_ms
                                      : kStaleFallbackDelayMs;
        SetNextDue(it->second, now + base);
      }
      continue;
    }

    RunStart(task_id);
  }

  done_waiter_->signal();
  sm::log::Write(sm::LogLevel::Info, "sched", "调度线程退出");
}

void Scheduler::ClearExecuting() {
  std::lock_guard<std::mutex> lock(mutex_);
  executing_.active = false;
  executing_.plugin_id.clear();
  executing_.watchdog_fired = false;
  executing_.removed = false;
}

void Scheduler::RunStart(const std::string& plugin_id) {
  IPluginRunner* const runner = runner_;
  if (runner == nullptr) {
    sm::log::Writef(sm::LogLevel::Warn, plugin_id.c_str(),
                    "调度器尚未接上插件管理器，跳过本次执行");
    return;
  }

  const std::int64_t begin = platform::monotonic_ms();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    executing_.active = true;
    executing_.plugin_id = plugin_id;
    executing_.begin_monotonic_ms = begin;
    executing_.watchdog_fired = false;
    executing_.removed = false;
  }

  struct ExecutingGuard {
    Scheduler* self;
    ~ExecutingGuard() { self->ClearExecuting(); }
  } guard{this};

  const StartCall call = runner->RunStart(plugin_id);
  const std::int64_t elapsed_ms = platform::monotonic_ms() - begin;

  if (call.crashed) {
    return;
  }

  bool         deliver = false;
  bool         watchdog_fired = false;
  std::string  disable_reason;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    watchdog_fired = executing_.watchdog_fired;

    auto it = slots_.find(plugin_id);
    if (it == slots_.end()) {
      sm::log::Writef(sm::LogLevel::Debug, plugin_id.c_str(),
                      "本次执行的结果被丢弃：插件已不在可调度集合中");
      return;
    }

    Slot&            slot = it->second;
    const std::int64_t run_delay_ms = slot.run_delay_ms;
    slot.run_delay_ms = 0;
    if (run_delay_ms > 0) slot.base_delay_ms = run_delay_ms;

    // 连续超时计数只在本次没被看门狗打断时清零：否则每轮都卡到超时的插件永远凑不满
    // 禁用阈值。
    if (!watchdog_fired) slot.timeouts = 0;

    switch (call.result) {
      case sm::StartResult::Ok:
        deliver = true;
        slot.failures = 0;
        slot.last_error.clear();
        break;

      case sm::StartResult::Empty:
        slot.failures = 0;
        slot.last_error.clear();
        break;

      case sm::StartResult::Cancelled:
        // 取消不算失败：不计退避，也不加连续失败计数。
        break;

      case sm::StartResult::Error: {
        ++slot.failures;
        const char* const plugin_reason = call.error_text();
        const char* const reason =
            (*plugin_reason != '\0') ? plugin_reason : "(插件未说明原因)";
        slot.last_error = reason;

        if (slot.failures >= limits_.failures_before_disable) {
          char text[256];
          std::snprintf(text, sizeof(text), "连续 %d 次执行失败（最后一次原因：%s）",
                        slot.failures, reason);
          disable_reason = text;
          break;
        }

        const std::int64_t base =
            (slot.base_delay_ms > 0) ? slot.base_delay_ms : kDefaultBackoffBaseMs;
        std::int64_t delay_ms = base;
        for (int i = 0; i < slot.failures; ++i) {
          if (delay_ms > limits_.backoff_cap_ms / 2) {
            delay_ms = limits_.backoff_cap_ms;
            break;
          }
          delay_ms *= 2;
        }
        if (delay_ms > limits_.backoff_cap_ms) delay_ms = limits_.backoff_cap_ms;
        if (run_delay_ms > 0 && run_delay_ms != slot.base_delay_ms) delay_ms = run_delay_ms;

        SetNextDue(slot, platform::monotonic_ms() + delay_ms);
        sm::log::Writef(sm::LogLevel::Warn, plugin_id.c_str(),
                        "本次执行失败（%s）；连续失败 %d 次，%.1f 秒后重试", reason,
                        slot.failures, delay_ms / 1000.0);
        break;
      }

      case sm::StartResult::Abort:
        disable_reason = "插件自报不可恢复的错误（Abort）";
        break;
    }
  }

  if (!disable_reason.empty()) {
    Disable(plugin_id, disable_reason);
  } else if (deliver) {
    if (notifications_ != nullptr) {
      notifications_->OnNotifications(plugin_id, call.items);
    } else {
      sm::log::Writef(sm::LogLevel::Debug, plugin_id.c_str(),
                      "产出 %zu 条通知，但通知管线尚未接入，本次产出被丢弃",
                      call.items.size);
    }
  }

  if (watchdog_fired) {
    sm::log::Writef(sm::LogLevel::Warn, plugin_id.c_str(),
                    "本次执行耗时 %.1f 秒，超过看门狗阈值 %.1f 秒",
                    elapsed_ms / 1000.0, limits_.watchdog_threshold_ms / 1000.0);
  }
}

void Scheduler::Disable(const std::string& plugin_id, const std::string& reason) {
  IPluginRunner* const runner = runner_;
  if (runner == nullptr) return;
  runner->DisablePlugin(plugin_id, reason.c_str(), false);
}

void Scheduler::PollWatchdog() {
  const std::int64_t now = platform::monotonic_ms();
  std::string        plugin_id;
  std::int64_t       elapsed_ms = 0;
  int                timeouts = 0;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now - last_watchdog_check_ms_ < limits_.watchdog_poll_interval_ms) return;
    last_watchdog_check_ms_ = now;

    if (!executing_.active || executing_.watchdog_fired) return;
    elapsed_ms = now - executing_.begin_monotonic_ms;
    if (elapsed_ms <= limits_.watchdog_threshold_ms) return;

    executing_.watchdog_fired = true;
    plugin_id = executing_.plugin_id;

    auto it = slots_.find(plugin_id);
    if (it != slots_.end()) {
      ++it->second.timeouts;
      timeouts = it->second.timeouts;
    }
  }

  sm::log::Writef(sm::LogLevel::Warn, plugin_id.c_str(),
                  "单次执行已进行 %.1f 秒（阈值 %.1f 秒），请求协作取消；连续超时 %d/%d",
                  elapsed_ms / 1000.0, limits_.watchdog_threshold_ms / 1000.0, timeouts,
                  limits_.watchdog_timeouts_before_disable);

  if (timeouts >= limits_.watchdog_timeouts_before_disable) {
    char text[160];
    std::snprintf(text, sizeof(text), "连续 %d 次执行超时（单次阈值 %.1f 秒）", timeouts,
                  limits_.watchdog_threshold_ms / 1000.0);
    Disable(plugin_id, text);
  }
}

std::vector<Scheduler::PluginSchedule> Scheduler::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::int64_t          now = platform::monotonic_ms();

  std::vector<PluginSchedule> result;
  result.reserve(slots_.size());
  for (const auto& entry : slots_) {
    PluginSchedule info;
    info.id = entry.first;
    info.scheduled = entry.second.heap_index != kNotInHeap;
    if (info.scheduled) {
      info.next_due_in_ms = entry.second.due_monotonic_ms - now;
      if (info.next_due_in_ms < 0) info.next_due_in_ms = 0;
    }
    info.consecutive_failures = entry.second.failures;
    info.consecutive_timeouts = entry.second.timeouts;
    info.last_error = entry.second.last_error;
    result.push_back(std::move(info));
  }
  return result;
}

bool Scheduler::IsScheduled(const std::string& plugin_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = slots_.find(plugin_id);
  return it != slots_.end() && it->second.heap_index != kNotInHeap;
}

bool Scheduler::IsAnyExecuting(std::string* out_plugin_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (out_plugin_id != nullptr) {
    *out_plugin_id = executing_.active ? executing_.plugin_id : std::string();
  }
  return executing_.active;
}

}  // namespace host
}  // namespace sm

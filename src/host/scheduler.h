// SimpleMessage — 调度器：最小堆、调度线程、看门狗标记
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/platform.h"
#include "sm/notification.h"
#include "sm/plugin.h"

#include "host/plugin_host.h"

namespace sm {
namespace host {

struct SchedulerLimits {
  std::int64_t watchdog_threshold_ms = 30 * 1000;
  int watchdog_timeouts_before_disable = 3;
  std::int64_t backoff_cap_ms = 30 * 60 * 1000;
  int failures_before_disable = 5;
  std::int64_t stale_grace_ms = 60 * 1000;
  std::int64_t watchdog_poll_interval_ms = 1000;
};

class Scheduler final : public IScheduleSink {
 public:
  explicit Scheduler(SchedulerLimits limits = SchedulerLimits());
  ~Scheduler() override;
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  void SetPluginRunner(IPluginRunner* runner);
  void SetNotificationSink(INotificationSink* sink);

  void start();
  void request_stop();
  bool join(std::int64_t grace_ms);
  bool running() const { return running_.load(); }

  void wake();
  void set_paused(bool paused);
  bool paused() const;

  void Add(const std::string& plugin_id) override;
  void ScheduleNext(const std::string& plugin_id, std::int64_t delay_ms) override;
  void ScheduleAt(const std::string& plugin_id, std::int64_t unix_ms) override;
  void Remove(const std::string& plugin_id) override;
  bool IsCancelled(const std::string& plugin_id) const override;
  bool IsExecuting(const std::string& plugin_id) const override;

  void PollWatchdog();

  struct PluginSchedule {
    std::string id;
    bool         scheduled = false;
    std::int64_t next_due_in_ms = -1;
    int          consecutive_failures = 0;
    int          consecutive_timeouts = 0;
    std::string  last_error;
  };
  std::vector<PluginSchedule> Snapshot() const;
  bool IsScheduled(const std::string& plugin_id) const;
  bool IsAnyExecuting(std::string* out_plugin_id = nullptr) const;

 private:
  static constexpr std::size_t kNotInHeap = static_cast<std::size_t>(-1);

  struct Slot {
    std::string  id;
    std::int64_t due_monotonic_ms = 0;
    std::size_t  heap_index = kNotInHeap;
    std::int64_t base_delay_ms = 0;
    std::int64_t run_delay_ms = 0;
    int          failures = 0;
    int          timeouts = 0;
    std::string  last_error;
  };

  struct HeapEntry {
    std::int64_t due_monotonic_ms = 0;
    std::string  plugin_id;
  };

  void SetNextDue(Slot& slot, std::int64_t due_monotonic_ms);
  void DropFromHeap(Slot& slot);
  void HeapSiftUp(std::size_t index);
  void HeapSiftDown(std::size_t index);
  void HeapSwap(std::size_t a, std::size_t b);

  void Loop();
  void RunStart(const std::string& plugin_id);
  void ClearExecuting();
  void Disable(const std::string& plugin_id, const std::string& reason);

  std::thread        thread_;
  platform::IWaiter* wake_waiter_ = nullptr;
  platform::IWaiter* done_waiter_ = nullptr;
  std::atomic<bool>  running_{false};
  bool abandoned_ = false;

  mutable std::mutex     mutex_;
  std::vector<HeapEntry> heap_;
  std::map<std::string, Slot> slots_;
  bool                   stopping_ = false;
  bool                   paused_ = false;

  struct Executing {
    bool         active = false;
    std::string  plugin_id;
    std::int64_t begin_monotonic_ms = 0;
    bool         watchdog_fired = false;
    bool         removed = false;
  };
  Executing    executing_;
  std::int64_t last_watchdog_check_ms_ = 0;

  IPluginRunner*     runner_ = nullptr;
  INotificationSink* notifications_ = nullptr;
  SchedulerLimits    limits_;
};

}  // namespace host
}  // namespace sm

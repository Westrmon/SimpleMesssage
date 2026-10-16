// _test.leak_thread —— initialize() 里起一个线程，exit() 里【故意】不 join 也不停。

#include <chrono>
#include <thread>

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.leak_thread", "泄漏测试：线程",
    "initialize() 起一个线程，exit() 里不停也不 join（T-EXC-19）。");

void KeepSpinning() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

class LeakThreadPlugin final : public sm::PluginBase {
 public:
  explicit LeakThreadPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override {
    if (thread_ == nullptr) {
      thread_ = new std::thread(&KeepSpinning);
      host()->log(sm::LogLevel::Warn,
                  "已起一个不会停的线程；exit() 不会 join 它 —— 这正是本用例要证明的错");
    }
    return true;
  }

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    host()->schedule_next(3600 * 1000);
    return sm::StartResult::Empty;
  }

  void exit() override {
    host()->log(sm::LogLevel::Warn, "exit() 故意不 join 线程，接下来卸载本模块");
  }

 private:
  std::thread* thread_ = nullptr;
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, LeakThreadPlugin);

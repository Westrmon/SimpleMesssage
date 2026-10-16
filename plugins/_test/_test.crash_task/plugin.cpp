// _test.crash_task —— 投给主线程的任务函数里崩溃。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.crash_task", "崩溃测试：主线程任务",
    "投给 post_to_main_thread 的任务函数里解引用空指针（T-EXC-4）。");

void CrashTask(void* user) {
  (void)user;
  sm_test::CrashNullDereference();
}

class CrashTaskPlugin final : public sm::PluginBase {
 public:
  explicit CrashTaskPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    if (!host()->post_to_main_thread(&CrashTask, nullptr)) {
      host()->set_error("投递主线程任务失败（队列满或未接线）");
    }
    host()->schedule_next(10 * 1000);
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, CrashTaskPlugin);

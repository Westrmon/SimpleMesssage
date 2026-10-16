// _test.crash_exit —— exit() 里崩溃。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.crash_exit", "崩溃测试：exit", "在 exit() 里解引用空指针（T-EXC-5）。");

class CrashExitPlugin final : public sm::PluginBase {
 public:
  explicit CrashExitPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    host()->schedule_next(3600 * 1000);
    return sm::StartResult::Empty;
  }

  void exit() override { sm_test::CrashNullDereference(); }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, CrashExitPlugin);

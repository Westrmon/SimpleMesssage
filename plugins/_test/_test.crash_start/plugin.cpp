// _test.crash_start —— 第一次 start() 里解引用空指针。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.crash_start", "崩溃测试：start", "在 start() 里解引用空指针（T-EXC-1）。");

class CrashStartPlugin final : public sm::PluginBase {
 public:
  explicit CrashStartPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    sm_test::CrashNullDereference();
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, CrashStartPlugin);

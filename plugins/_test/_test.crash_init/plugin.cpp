// _test.crash_init —— initialize() 里崩溃。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.crash_init", "崩溃测试：initialize", "在 initialize() 里解引用空指针（T-EXC-2）。");

class CrashInitPlugin final : public sm::PluginBase {
 public:
  explicit CrashInitPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override {
    sm_test::CrashNullDereference();
    return true;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, CrashInitPlugin);

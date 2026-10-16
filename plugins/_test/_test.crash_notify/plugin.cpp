// _test.crash_notify —— on_notify() 里崩溃。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::NotifierMeta(
    "_test.crash_notify", "崩溃测试：on_notify",
    "收到任何通知就解引用空指针（T-EXC-3）。");

class CrashNotifyPlugin final : public sm::PluginBase {
 public:
  explicit CrashNotifyPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  void on_notify(const sm::Notification& n) override {
    (void)n;
    sm_test::CrashNullDereference();
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, CrashNotifyPlugin);

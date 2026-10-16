// _test.log_loop —— 在 on_notify() 里写 Error 日志。

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("logs_per_notify", 1, 1, 20, "每条通知写几条日志",
                         "调大能更快看出递归是否被止住。"),
};

constexpr sm::PluginMeta kMeta = sm_test::NotifierMeta(
    "_test.log_loop", "通知测试：日志回环",
    "收到通知就写 Error 日志，用来验证日志桥的递归中断（T-INT-6）。",
    sm::span_of(kSettings));

class LogLoopPlugin final : public sm::PluginBase {
 public:
  explicit LogLoopPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  void on_notify(const sm::Notification& n) override {
    const std::int64_t count = host()->setting_int("logs_per_notify", 1);
    for (std::int64_t i = 0; i < count; ++i) {
      host()->logf(sm::LogLevel::Error, "回环测试：这是对通知 #%lld 的回写 %lld",
                   static_cast<long long>(n.seq), static_cast<long long>(i));
    }
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, LogLoopPlugin);

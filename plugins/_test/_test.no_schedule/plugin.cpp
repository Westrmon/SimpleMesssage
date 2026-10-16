// _test.no_schedule —— start() 里【不】调用 schedule_next()。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.no_schedule", "调度测试：不注册",
    "start() 不调用 schedule_next，验证此后彻底不再被调度。");

class NoSchedulePlugin final : public sm::PluginBase {
 public:
  explicit NoSchedulePlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    out->items = sm::Span<const sm::Notification>{};
    out->error = nullptr;
    host()->log(sm::LogLevel::Warn,
                "本次没有注册下一次调度：本插件从现在起休眠，直到重新启用它");
    return sm_test::ReturnOne(host(), out, "test.no_schedule",
                              "这是它最后一次说话", "没有 schedule_next 就没有下一次。");
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, NoSchedulePlugin);

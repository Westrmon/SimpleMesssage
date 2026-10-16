// _test.hang_cancellable —— 卡住，但每 100 毫秒看一眼 is_cancelled()。

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("hang_seconds", 60, 1, 600, "最长等待（秒）",
                         "最多睡这么久；等待期间每 100 毫秒检查一次取消标志。"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.hang_cancellable", "卡死测试：可协作取消",
    "在 start() 里长时间等待，但响应 is_cancelled()（T-EXC-13）。",
    sm::span_of(kSettings));

class HangCancellablePlugin final : public sm::PluginBase {
 public:
  explicit HangCancellablePlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    const std::int64_t seconds = host()->setting_int("hang_seconds", 60);
    host()->logf(sm::LogLevel::Info, "开始等待 %lld 秒（可被取消）",
                 static_cast<long long>(seconds));

    if (!sm_test::SleepCancellable(host(), seconds * 1000)) {
      host()->log(sm::LogLevel::Info, "收到取消，提前返回 Cancelled");
      host()->schedule_next(60 * 1000);
      return sm::StartResult::Cancelled;
    }

    host()->log(sm::LogLevel::Info, "等满了，没有被取消");
    host()->schedule_next(60 * 1000);
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, HangCancellablePlugin);

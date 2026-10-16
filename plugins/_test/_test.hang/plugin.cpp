// _test.hang —— start() 里睡死，且【故意不检查】is_cancelled()。

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("hang_seconds", 40, 1, 600, "卡住时长（秒）",
                         "start() 里睡这么久，并且不检查 is_cancelled()。"
                         "超过看门狗阈值（30 秒）就会把本插件标记为无响应。"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.hang", "卡死测试：不协作", "在 start() 里睡死且不响应取消（T-EXC-11/12/14）。",
    sm::span_of(kSettings));

class HangPlugin final : public sm::PluginBase {
 public:
  explicit HangPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    const std::int64_t seconds = host()->setting_int("hang_seconds", 40);
    host()->logf(sm::LogLevel::Warn, "开始卡住 %lld 秒，且不看取消标志",
                 static_cast<long long>(seconds));
    sm_test::SleepMs(seconds * 1000);
    host()->logf(sm::LogLevel::Warn, "卡住结束（宿主没能打断我，也没打算打断我）");
    host()->schedule_next(60 * 1000);
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, HangPlugin);

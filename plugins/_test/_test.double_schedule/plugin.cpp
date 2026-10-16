// _test.double_schedule —— 一次 start() 里调用两次 schedule_next()。

#include <cstdio>

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("first_ms", 1000, 1, 86400000, "第一次注册的间隔（毫秒）"),
    sm::setting::integer("second_ms", 5000, 1, 86400000, "第二次注册的间隔（毫秒）"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.double_schedule", "调度测试：重复注册",
    "一次 start() 里注册两次调度（以最后一次为准）。", sm::span_of(kSettings));

class DoubleSchedulePlugin final : public sm::PluginBase {
 public:
  explicit DoubleSchedulePlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    const std::int64_t first = host()->setting_int("first_ms", 1000);
    const std::int64_t second = host()->setting_int("second_ms", 5000);

    host()->schedule_next(first);
    host()->schedule_next(second);

    char message[160] = {};
    std::snprintf(message, sizeof(message),
                  "注册了 %lld ms，随后改注册为 %lld ms；生效的应当是后者",
                  static_cast<long long>(first), static_cast<long long>(second));
    host()->log(sm::LogLevel::Info, message);
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, DoubleSchedulePlugin);

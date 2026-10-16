// _test.spam —— 一次 start() 返回 2000 条通知。

#include <cstdio>

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("count", 2000, 1, 5000, "条数", "本次 start() 产生多少条通知。"),
    sm::setting::integer("period_sec", 30, 1, 86400, "间隔（秒）"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.spam", "压力测试：通知洪水", "一次产生大量通知（背压与丢最旧）。",
    sm::span_of(kSettings));

class SpamPlugin final : public sm::PluginBase {
 public:
  explicit SpamPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    const std::int64_t count = host()->setting_int("count", 2000);
    const std::int64_t period = host()->setting_int("period_sec", 30);

    auto* const items = host()->allocate_array<sm::Notification>(
        static_cast<std::size_t>(count));
    if (items == nullptr) {
      host()->set_error("arena 装不下这么多通知；把 count 调小");
      host()->schedule_next(period * 1000);
      return sm::StartResult::Error;
    }

    char title[64] = {};
    char body[96] = {};
    for (std::int64_t i = 0; i < count; ++i) {
      std::snprintf(title, sizeof(title), "spam #%lld", static_cast<long long>(i));
      std::snprintf(body, sizeof(body), "第 %lld 条洪水通知", static_cast<long long>(i));
      if (!sm_test::FillNotification(host(), &items[i], "test.spam", title, body,
                                     sm::Level::Info)) {
        host()->logf(sm::LogLevel::Warn, "只装下了 %lld 条", static_cast<long long>(i));
        out->items = sm::Span<const sm::Notification>(items, static_cast<std::size_t>(i));
        host()->schedule_next(period * 1000);
        return (i > 0) ? sm::StartResult::Ok : sm::StartResult::Error;
      }
    }

    out->items = sm::Span<const sm::Notification>(items, static_cast<std::size_t>(count));
    host()->logf(sm::LogLevel::Info, "本次产生 %lld 条", static_cast<long long>(count));
    host()->schedule_next(period * 1000);
    return sm::StartResult::Ok;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, SpamPlugin);

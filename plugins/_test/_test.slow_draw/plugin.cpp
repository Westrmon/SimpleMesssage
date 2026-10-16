// _test.slow_draw —— draw_ui() 里花 200 毫秒。

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("draw_ms", 200, 1, 5000, "每帧耗时（毫秒）"),
    sm::setting::integer("hold_seconds", 0, 0, 3600, "持有时长（秒）",
                         "0 表示持续持有（只能由 stop()/exit() 归还）。"),
};

constexpr sm::PluginMeta kMeta = sm_test::NotifierMeta(
    "_test.slow_draw", "界面测试：慢绘制",
    "draw_ui 每帧睡 200 毫秒，用来观察界面卡顿的边界（T-EXC-15）。",
    sm::span_of(kSettings));

class SlowDrawPlugin final : public sm::PluginBase {
 public:
  explicit SlowDrawPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override {
    frames_ = 0;
    const std::int64_t hold = host()->setting_int("hold_seconds", 0);
    host()->request_ui_surface(hold <= 0 ? 0 : hold * 1000);
    host()->logf(sm::LogLevel::Warn, "已请求 UI 引用（%s），接下来每帧都会拖慢界面",
                 hold <= 0 ? "持续" : "限时");
    return true;
  }

  void stop() override {
    host()->close_view();
  }

  void draw_ui(sm::Span<const sm::Notification> history) override {
    (void)history;
    ++frames_;
    sm_test::SleepMs(host()->setting_int("draw_ms", 200));
    if (frames_ % 10 == 0) {
      host()->logf(sm::LogLevel::Debug, "已拖慢 %lld 帧", static_cast<long long>(frames_));
    }
  }

 private:
  std::int64_t frames_ = 0;
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, SlowDrawPlugin);

// _test.ui_ref —— 请求三种 UI 引用：5 秒、60 秒、持续。

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::boolean("ref_5s", true, "请求 5 秒引用"),
    sm::setting::boolean("ref_60s", true, "请求 60 秒引用"),
    sm::setting::boolean("ref_forever", false, "请求持续引用（只能由 close_view 归还）"),
};

constexpr sm::PluginMeta kMeta = sm_test::NotifierMeta(
    "_test.ui_ref", "通知测试：UI 引用",
    "请求 5 秒 / 60 秒 / 持续三种 UI 引用（T-UI-5）。", sm::span_of(kSettings));

class UiRefPlugin final : public sm::PluginBase {
 public:
  explicit UiRefPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override {
    if (host()->setting_bool("ref_5s", true)) {
      host()->request_ui_surface(5 * 1000);
      host()->log(sm::LogLevel::Info, "已请求 5 秒 UI 引用");
    }
    if (host()->setting_bool("ref_60s", true)) {
      host()->request_ui_surface(60 * 1000);
      host()->log(sm::LogLevel::Info, "已请求 60 秒 UI 引用");
    }
    if (host()->setting_bool("ref_forever", false)) {
      host()->set_view_title("测试：UI 引用（持续）");
      host()->request_ui_surface(0);
      host()->log(sm::LogLevel::Info, "已请求持续 UI 引用与视图；只有 close_view() 能归还");
    }
    return true;
  }

  void stop() override {
    host()->close_view();
    host()->log(sm::LogLevel::Info, "已归还本插件的全部 UI 引用");
  }

  void draw_ui(sm::Span<const sm::Notification> history) override { (void)history; }

  void draw_view() override {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, UiRefPlugin);

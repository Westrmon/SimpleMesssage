// _badmeta.too_many_subs —— 20 个订阅模式（上限 16）。

#include "../_badmeta_support.h"

namespace {

constexpr const char* kSubscriptions[] = {
    "t00.*", "t01.*", "t02.*", "t03.*", "t04.*",
    "t05.*", "t06.*", "t07.*", "t08.*", "t09.*",
    "t10.*", "t11.*", "t12.*", "t13.*", "t14.*",
    "t15.*", "t16.*", "t17.*", "t18.*", "t19.*",
};

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.too_many_subs", "订阅模式有 20 个，超过 16 个的上限。");
  meta.kind = sm::PluginKind::Notifier;
  meta.subscriptions = sm::span_of(kSubscriptions);
  return meta;
}();

class TooManySubsPlugin final : public sm::PluginBase {
 public:
  explicit TooManySubsPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, TooManySubsPlugin);

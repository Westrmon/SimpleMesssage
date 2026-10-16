// _badmeta.range_syntax —— host_range = ">=1.0.0,,"（语法非法）。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.range_syntax", "host_range 的语法本身是错的。");
  meta.host_range = ">=1.0.0,,";
  return meta;
}();

class RangeSyntaxPlugin final : public sm::PluginBase {
 public:
  explicit RangeSyntaxPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, RangeSyntaxPlugin);

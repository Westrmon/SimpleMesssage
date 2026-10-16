// _badmeta.range_miss —— host_range = ">=99.0.0"（语法合法但不满足）。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.range_miss", "要求的宿主版本远高于当前版本。");
  meta.host_range = ">=99.0.0";
  return meta;
}();

class RangeMissPlugin final : public sm::PluginBase {
 public:
  explicit RangeMissPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, RangeMissPlugin);

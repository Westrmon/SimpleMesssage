// _badmeta.meta_abi —— PluginMeta::api_version = 0（与导出函数不一致）。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta =
      sm_badmeta::BaseMeta("_badmeta.meta_abi", "元信息里的 api_version 是 0。");
  meta.api_version = 0;
  return meta;
}();

class MetaAbiPlugin final : public sm::PluginBase {
 public:
  explicit MetaAbiPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, MetaAbiPlugin);

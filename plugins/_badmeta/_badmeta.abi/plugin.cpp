// _badmeta.abi —— sm_abi_version() 返回 SM_API_VERSION + 1。

#include <cstdint>

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_badmeta::BaseMeta(
    "_badmeta.abi", "sm_abi_version() 返回一个比宿主新的 ABI 版本。");

class AbiPlugin final : public sm::PluginBase {
 public:
  explicit AbiPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_PLUGIN_EXPORT std::int32_t sm_abi_version() {
  return SM_API_VERSION + 1;
}

SM_PLUGIN_EXPORT const sm::PluginMeta* sm_plugin_meta() {
  return &kMeta;
}

SM_PLUGIN_EXPORT sm::IPlugin* sm_create_plugin(sm::IHostContext* host) {
  return new AbiPlugin(host);
}

SM_PLUGIN_EXPORT void sm_destroy_plugin(sm::IPlugin* plugin) {
  delete plugin;
}

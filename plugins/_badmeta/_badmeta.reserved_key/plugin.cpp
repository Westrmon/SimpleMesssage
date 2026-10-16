// _badmeta.reserved_key —— 设置项名叫 "schema_version"。

#include "../_badmeta_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::integer("schema_version", 1, 0, 100, "占用了宿主的保留键"),
};

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.reserved_key", "设置项使用了宿主保留的键名。");
  meta.settings = sm::span_of(kSettings);
  return meta;
}();

class ReservedKeyPlugin final : public sm::PluginBase {
 public:
  explicit ReservedKeyPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, ReservedKeyPlugin);

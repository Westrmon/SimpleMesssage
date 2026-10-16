// _badmeta.empty_enum —— Enum 设置项的 options 为空。

#include "../_badmeta_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::enumeration("mode", "a", sm::Span<const sm::SettingOption>{},
                             "没有任何选项的枚举"),
};

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.empty_enum", "Enum 设置项没有给出任何选项。");
  meta.settings = sm::span_of(kSettings);
  return meta;
}();

class EmptyEnumPlugin final : public sm::PluginBase {
 public:
  explicit EmptyEnumPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, EmptyEnumPlugin);

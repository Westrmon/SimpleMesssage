// _badmeta.dup_setting —— 两个设置项都叫 "foo"。

#include "../_badmeta_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::boolean("foo", true, "第一个 foo"),
    sm::setting::integer("foo", 1, 0, 10, "第二个 foo（重名）"),
};

constexpr sm::PluginMeta kMeta = [] {
  sm::PluginMeta meta = sm_badmeta::BaseMeta(
      "_badmeta.dup_setting", "两个设置项使用了同一个名字。");
  meta.settings = sm::span_of(kSettings);
  return meta;
}();

class DupSettingPlugin final : public sm::PluginBase {
 public:
  explicit DupSettingPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, DupSettingPlugin);

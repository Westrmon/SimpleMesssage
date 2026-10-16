// _badmeta.badid —— id 含大写字母与空格。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_badmeta::BaseMeta(
    "Test Bad Id", "id 含大写字母与空格。");

class BadIdPlugin final : public sm::PluginBase {
 public:
  explicit BadIdPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, BadIdPlugin);

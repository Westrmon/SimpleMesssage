// _badmeta.namespace —— id = "sm.host"。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_badmeta::BaseMeta(
    "sm.host", "占用了宿主的保留命名空间。");

class NamespacePlugin final : public sm::PluginBase {
 public:
  explicit NamespacePlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, NamespacePlugin);

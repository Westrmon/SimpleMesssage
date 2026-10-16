// _badmeta.filename —— 文件名是 _badmeta.filename.dll，id 却是 "test.mismatch"。

#include "../_badmeta_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_badmeta::BaseMeta(
    "test.mismatch", "文件名与 id 不一致。");

class FilenamePlugin final : public sm::PluginBase {
 public:
  explicit FilenamePlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, FilenamePlugin);

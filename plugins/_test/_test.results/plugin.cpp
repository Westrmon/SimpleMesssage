// _test.results —— 按设置返回指定的 StartResult 序列。

#include "../_test_results_base.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::text("sequence", "empty,error,error,abort", 256, "结果序列",
                      "逗号分隔，可用值：ok / empty / error / abort / cancelled。"
                      "用完循环；重新启用插件会把计数清零。"),
    sm::setting::integer("period_ms", 500, 1, 3600000, "间隔（毫秒）"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.results", "调度测试：结果序列",
    "按设置依次返回 Ok/Empty/Error/Abort/Cancelled（退避、禁用、计数清零）。",
    sm::span_of(kSettings));

class ResultsPlugin final : public sm_test::ResultsBase {
 public:
  explicit ResultsPlugin(sm::IHostContext* host) : ResultsBase(host, kMeta) {}
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, ResultsPlugin);

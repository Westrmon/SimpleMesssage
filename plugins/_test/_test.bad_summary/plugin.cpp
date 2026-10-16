// _test.bad_summary —— 正文含连续空白与超长中文，且不填 summary。

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.bad_summary", "压力测试：摘要生成",
    "提交含连续空白与超长中文、且不填 summary 的通知（宿主自动摘要）。");

class BadSummaryPlugin final : public sm::PluginBase {
 public:
  explicit BadSummaryPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    out->items = sm::Span<const sm::Notification>{};
    out->error = nullptr;

    std::string content;
    content += "\n\n\n        第一行前面有空白，中间有一串空格         \n\n\t\t";
    content += sm_test::Repeat("这是一段很长的中文正文，用来验证摘要不会把汉字切坏。", 2000);
    content += "\t\t  结尾也是空白  \n";
    return sm_test::ReturnOne(host(), out, "test.bad_summary",
                              "摘要在大多数情况下够用来判断要不要打开详情",
                              content.c_str(), sm::Level::Info);
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, BadSummaryPlugin);

// _test.long_fields —— 返回超过上限的 title / content / topic / url / actions。

#include <cstdio>

#include "../_test_support.h"

namespace {

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.long_fields", "压力测试：超长字段",
    "提交远超上限的 title/content/topic/url 与动作数（截断 + Warn）。");

class LongFieldsPlugin final : public sm::PluginBase {
 public:
  explicit LongFieldsPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    const std::string title = sm_test::Repeat("标题", 400);
    const std::string content = sm_test::Repeat("内容内容内容", 70 * 1024);
    const std::string topic = "test.long_" + sm_test::Repeat("topic", 300);
    const std::string url = "https://example.invalid/" + sm_test::Repeat("u", 3000);
    constexpr std::size_t kActionCount = 6;

    auto* const items = host()->allocate_array<sm::Notification>(1);
    if (items == nullptr) {
      host()->set_error("arena 分配失败（通知数组）");
      host()->schedule_next(600 * 1000);
      return sm::StartResult::Error;
    }

    sm::Notification& n = items[0];
    n = sm::Notification{};
    n.title = host()->allocate_string(title.c_str(), title.size());
    n.content = host()->allocate_string(content.c_str(), content.size());
    n.topic = host()->allocate_string(topic.c_str(), topic.size());
    n.url = host()->allocate_string(url.c_str(), url.size());
    n.summary = nullptr;
    n.level = sm::Level::Warning;
    n.time_unix_ms = host()->now_unix_ms();
    if (n.title == nullptr || n.content == nullptr || n.topic == nullptr || n.url == nullptr) {
      host()->set_error("arena 分配失败（字段）");
      host()->schedule_next(600 * 1000);
      return sm::StartResult::Error;
    }

    sm::Action* const actions = host()->allocate_array<sm::Action>(kActionCount);
    if (actions == nullptr) {
      host()->set_error("arena 分配失败（动作数组）");
      host()->schedule_next(600 * 1000);
      return sm::StartResult::Error;
    }
    for (std::size_t i = 0; i < kActionCount; ++i) {
      static const char* const kIds[] = {"a0", "a1", "a2", "a3", "a4", "a5"};
      static const char* const kLabels[] = {"动作 0", "动作 1", "动作 2",
                                           "动作 3", "动作 4", "动作 5"};
      actions[i].id = kIds[i];
      actions[i].label = kLabels[i];
    }
    n.actions = sm::Span<sm::Action>(actions, kActionCount);

    char message[256] = {};
    std::snprintf(message, sizeof(message),
                  "本次提交：title %zu / content %zu / topic %zu / url %zu 字节，动作 %zu 个",
                  title.size(), content.size(), topic.size(), url.size(), kActionCount);
    host()->log(sm::LogLevel::Info, message);

    out->items = sm::Span<const sm::Notification>(items, 1);
    host()->schedule_next(600 * 1000);
    return sm::StartResult::Ok;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, LongFieldsPlugin);

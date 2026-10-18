// SimpleMessage 示例插件 —— notifier.console

#include <cstring>

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"

namespace {

using sm::PluginBase;
using sm::PluginKind;
using sm::PluginMeta;
using sm::SettingDef;
using sm::SettingOption;
using sm::StartOutput;
using sm::StartResult;

constexpr SettingOption kMinLevelOptions[] = {
    {"info", "Info 及以上"},
    {"warning", "Warning 及以上"},
    {"error", "Error 及以上"},
};

constexpr SettingOption kFormatOptions[] = {
    {"oneline", "单行"},
    {"block", "多行块"},
};

constexpr SettingDef kSettings[] = {
    sm::setting::enumeration("min_level", "warning", sm::span_of(kMinLevelOptions),
                             "最低记录级别",
                             "低于该级别的通知不会写进日志。默认只记 Warning 以上，"
                             "避免日志被通知内容淹没。"),
    sm::setting::enumeration("format", "oneline", sm::span_of(kFormatOptions),
                             "输出格式"),
    sm::setting::boolean("include_source", true, "附带来源插件 id"),
};

constexpr PluginMeta kMeta = {
     "notifier.console",
     "日志通知",
     "SimpleMessage 示例",
     "1.0.0",
     "把匹配的通知写进宿主日志，不使用任何 UI 与第三方依赖。",
     PluginKind::Notifier,
     ">=0.1.0,<1.0.0",
     sm::Span<const char*>{},
     sm::span_of(kSettings),
     SM_API_VERSION,
};

sm::LogLevel ToLogLevel(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return sm::LogLevel::Trace;
    case sm::Level::Info:     return sm::LogLevel::Info;
    case sm::Level::Warning:  return sm::LogLevel::Warn;
    case sm::Level::Error:    return sm::LogLevel::Warn;
    case sm::Level::Critical: return sm::LogLevel::Warn;
  }
  return sm::LogLevel::Info;
}

int MinLevelThreshold(const char* setting_value) {
  if (setting_value != nullptr) {
    if (std::strcmp(setting_value, "info") == 0) return static_cast<int>(sm::Level::Info);
    if (std::strcmp(setting_value, "warning") == 0) return static_cast<int>(sm::Level::Warning);
    if (std::strcmp(setting_value, "error") == 0) return static_cast<int>(sm::Level::Error);
  }
  return static_cast<int>(sm::Level::Warning);
}

class ConsoleNotifier final : public PluginBase {
 public:
  explicit ConsoleNotifier(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  void on_notify(const sm::Notification& n) override {
    if (static_cast<int>(n.level) < MinLevelThreshold(host()->setting_enum("min_level", "warning"))) {
      return;
    }

    const sm::LogLevel level = ToLogLevel(n.level);
    const char* const summary = (n.summary != nullptr) ? n.summary : "";
    const char* const title = (n.title != nullptr) ? n.title : "";
    const char* const topic = (n.topic != nullptr) ? n.topic : "";

    const bool with_source = host()->setting_bool("include_source", true);
    const char* const source = with_source ? n.source_plugin : nullptr;

    if (std::strcmp(host()->setting_enum("format", "oneline"), "block") == 0) {
      if (source != nullptr && source[0] != '\0') {
        host()->logf(level, "通知 [%s] %s / 来源 %s", topic, title, source);
      } else {
        host()->logf(level, "通知 [%s] %s", topic, title);
      }
      host()->logf(level, "  摘要：%s", summary);
      if (n.url != nullptr && n.url[0] != '\0') host()->logf(level, "  链接：%s", n.url);
      return;
    }

    if (source != nullptr && source[0] != '\0') {
      host()->logf(level, "[%s] %s :: %s（来源 %s）", topic, title, summary, source);
    } else {
      host()->logf(level, "[%s] %s :: %s", topic, title, summary);
    }

  }

};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, ConsoleNotifier);

// SimpleMessage — 通知的过滤、序列化与绘制
#include "host/notify_view.h"

#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "host/log.h"
#include "host/strings.h"
#include "host/util.h"
#include "platform/fs.h"

namespace sm {
namespace host {
namespace {

constexpr std::uint32_t kColorDebug = 0xFFA0A0A0u;
constexpr std::uint32_t kColorInfo = 0xFFE0B070u;
constexpr std::uint32_t kColorWarning = 0xFF40C8E0u;
constexpr std::uint32_t kColorError = 0xFF3090F0u;
constexpr std::uint32_t kColorCritical = 0xFF4040E0u;
constexpr std::uint32_t kColorTrace = 0xFF808080u;

bool Contains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  return haystack.find(needle) != std::string::npos;
}

bool StartsWith(const std::string& text, const std::string& prefix) {
  return prefix.empty() || (text.size() >= prefix.size() &&
                            text.compare(0, prefix.size(), prefix) == 0);
}

float RowHeight() { return ImGui::GetTextLineHeightWithSpacing(); }

float TableRowHeight() {
  return RowHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
}

}  // namespace

std::uint32_t LevelColor(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return kColorDebug;
    case sm::Level::Info:     return kColorInfo;
    case sm::Level::Warning:  return kColorWarning;
    case sm::Level::Error:    return kColorError;
    case sm::Level::Critical: return kColorCritical;
  }
  return kColorInfo;
}

std::uint32_t LogLevelColor(sm::LogLevel level) {
  switch (level) {
    case sm::LogLevel::Trace: return kColorTrace;
    case sm::LogLevel::Debug: return kColorDebug;
    case sm::LogLevel::Info:  return kColorInfo;
    case sm::LogLevel::Warn:  return kColorWarning;
    case sm::LogLevel::Error: return kColorError;
  }
  return kColorInfo;
}

bool MatchesNotifyFilter(const NotifyFilter& filter, const OwnedNotification& n) {
  if (!filter.levels.Has(n.level)) return false;
  if (!filter.source.empty() && filter.source != n.source_plugin) return false;
  if (!StartsWith(n.topic, filter.topic)) return false;
  if (!filter.text.empty() && !Contains(n.title, filter.text) &&
      !Contains(n.content, filter.text) && !Contains(n.summary, filter.text)) {
    return false;
  }
  return true;
}

bool MatchesLogFilter(const LogFilter& filter, const log::Entry& entry) {
  if (!filter.levels.Has(entry.level)) return false;
  if (!filter.source.empty() && filter.source != entry.source) return false;
  if (!filter.text.empty() && !Contains(entry.message, filter.text)) return false;
  return true;
}

bool TopicHasSubscribers(const Router* router, const std::string& topic) {
  if (router == nullptr) return false;
  std::vector<std::string> matched;
  router->Match(topic, &matched);
  return !matched.empty();
}

namespace {

void AppendJsonString(std::string* out, const std::string& text) {
  out->push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"':  *out += "\\\""; break;
      case '\\': *out += "\\\\"; break;
      case '\n': *out += "\\n"; break;
      case '\r': *out += "\\r"; break;
      case '\t': *out += "\\t"; break;
      case '\b': *out += "\\b"; break;
      case '\f': *out += "\\f"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          *out += escape;
        } else {
          out->push_back(c);
        }
        break;
    }
  }
  out->push_back('"');
}

void AppendKey(std::string* out, const char* key) {
  out->push_back('"');
  *out += key;
  *out += "\":";
}

void AppendStringField(std::string* out, const char* key, const std::string& value) {
  AppendKey(out, key);
  AppendJsonString(out, value);
}

void AppendIntField(std::string* out, const char* key, std::int64_t value) {
  AppendKey(out, key);
  *out += std::to_string(value);
}

}  // namespace

std::string NotificationJsonLine(const OwnedNotification& n) {
  std::string line;
  line.reserve(n.title.size() + n.content.size() + 128);
  line.push_back('{');
  AppendIntField(&line, "seq", n.seq);
  line.push_back(',');
  AppendIntField(&line, "time_unix_ms", n.time_unix_ms);
  line.push_back(',');
  AppendStringField(&line, "level", LevelKey(n.level));
  line.push_back(',');
  AppendIntField(&line, "importance", n.importance);
  line.push_back(',');
  AppendStringField(&line, "source_plugin", n.source_plugin);
  line.push_back(',');
  AppendStringField(&line, "topic", n.topic);
  line.push_back(',');
  AppendStringField(&line, "id", n.id);
  line.push_back(',');
  AppendStringField(&line, "title", n.title);
  line.push_back(',');
  AppendStringField(&line, "summary", n.summary);
  line.push_back(',');
  AppendStringField(&line, "content", n.content);
  line.push_back(',');
  AppendStringField(&line, "url", n.url);
  line.push_back(',');
  AppendStringField(&line, "icon", n.icon);
  line.push_back(',');

  AppendKey(&line, "actions");
  line.push_back('[');
  for (std::size_t i = 0; i < n.actions.size(); ++i) {
    if (i > 0) line.push_back(',');
    line.push_back('{');
    AppendStringField(&line, "id", n.actions[i].id);
    line.push_back(',');
    AppendStringField(&line, "label", n.actions[i].label);
    line.push_back('}');
  }
  line += "]}";
  return line;
}

bool WriteNotificationsJsonl(const std::string& utf8_path,
                             const std::vector<const OwnedNotification*>& items,
                             std::string* out_error) {
  if (utf8_path.empty()) {
    if (out_error != nullptr) *out_error = "路径为空";
    return false;
  }

  std::FILE* file = platform::fs::open_file(utf8_path.c_str(), "wb");
  if (file == nullptr) {
    if (out_error != nullptr) *out_error = "无法创建文件 " + utf8_path;
    return false;
  }

  bool ok = true;
  for (const OwnedNotification* const n : items) {
    if (n == nullptr) continue;
    const std::string line = NotificationJsonLine(*n);
    if (std::fwrite(line.data(), 1, line.size(), file) != line.size() ||
        std::fwrite("\n", 1, 1, file) != 1) {
      ok = false;
      break;
    }
  }
  if (std::fflush(file) != 0) ok = false;
  if (std::fclose(file) != 0) ok = false;

  if (!ok && out_error != nullptr) *out_error = "写入失败 " + utf8_path;
  return ok;
}

std::int64_t DrawNotificationList(const char* table_id,
                                  const std::vector<const OwnedNotification*>& items,
                                  std::int64_t selected_seq) {
  std::int64_t clicked_seq = 0;
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  const ImVec2 size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f);

  if (!ImGui::BeginTable(table_id, 6, flags, size)) return 0;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(strings::kColTime, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(strings::kColLevel, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(strings::kColSource, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(strings::kColTopic, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(strings::kColTitle, ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(strings::kColSummary, ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();

  const std::int64_t now = util::now_unix_ms();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(items.size()), TableRowHeight());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const OwnedNotification& n = *items[static_cast<std::size_t>(row)];
      char time_text[32];
      util::format_short_time(n.time_unix_ms, now, time_text, sizeof(time_text));

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool selected = (n.seq == selected_seq);
      char selectable_id[32];
      std::snprintf(selectable_id, sizeof(selectable_id), "##row%lld",
                    static_cast<long long>(n.seq));
      if (ImGui::Selectable(selectable_id, selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0.0f, RowHeight()))) {
        clicked_seq = n.seq;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(time_text);

      ImGui::TableSetColumnIndex(1);
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(LevelColor(n.level)), "%s",
                         strings::LevelText(n.level));
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(n.source_plugin.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(n.topic.c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(n.title.c_str());
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(n.summary.c_str());
    }
  }
  ImGui::EndTable();
  return clicked_seq;
}

namespace {

// ImGui 没有可选中复制的静态文本控件；正文改用只读多行输入框承载。
// 同一时刻只绘制一条详情，因此静态缓冲即可；同 seq 的通知可能被原地更新，按内容判断。
std::vector<char>& DetailContentBuffer(const OwnedNotification& n) {
  static std::vector<char> buffer;
  static std::string cached;
  if (cached != n.content || buffer.size() != n.content.size() + 1) {
    buffer.assign(n.content.begin(), n.content.end());
    buffer.push_back('\0');
    cached = n.content;
  }
  return buffer;
}

}  // namespace

bool DrawNotificationDetail(const char* id_scope, const OwnedNotification& n,
                            const Router* router, IActionDispatcher* dispatcher) {
  ImGui::PushID(id_scope != nullptr ? id_scope : "");
  ImGui::PushID(static_cast<int>(n.seq));

  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(LevelColor(n.level)), "%s",
                     strings::LevelText(n.level));
  ImGui::SameLine();
  ImGui::TextUnformatted(n.title.c_str());

  char time_text[64];
  util::format_short_time(n.time_unix_ms, util::now_unix_ms(), time_text,
                          sizeof(time_text));
  ImGui::TextDisabled("%s · %s", n.source_plugin.c_str(), time_text);
  if (!n.topic.empty()) ImGui::TextDisabled("%s", n.topic.c_str());
  if (n.importance != 0) {
    ImGui::TextDisabled("重要等级 %d", static_cast<int>(n.importance));
  }
  if (!n.id.empty()) ImGui::TextDisabled("id=%s", n.id.c_str());

  ImGui::SeparatorText(strings::kDetailContent);
  const float content_height = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
  std::vector<char>& content = DetailContentBuffer(n);
  ImGui::InputTextMultiline("##content", content.data(), content.size(),
                            ImVec2(ImGui::GetContentRegionAvail().x, content_height),
                            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

  if (!n.url.empty()) {
    ImGui::TextDisabled("%s", n.url.c_str());
  }

  ImGui::SeparatorText(strings::kDetailActions);
  bool clicked = false;
  if (n.actions.empty()) {
    ImGui::TextDisabled("%s", strings::kDetailNoAction);
  } else {
    if (!TopicHasSubscribers(router, n.topic)) {
      ImGui::TextWrapped("%s", strings::kDetailUnrouted);
    }
    for (const OwnedAction& action : n.actions) {
      if (action.id.empty()) continue;
      const char* const label = action.label.empty() ? action.id.c_str() : action.label.c_str();
      if (ImGui::Button(label)) {
        if (dispatcher != nullptr) dispatcher->PostAction(n.seq, action.id.c_str());
        clicked = true;
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();
  }

  ImGui::TextDisabled("seq=%lld", static_cast<long long>(n.seq));

  ImGui::PopID();
  ImGui::PopID();
  return clicked;
}

}  // namespace host
}  // namespace sm

// SimpleMessage — 控制面板状态的读写

#include "host/ui_state.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <toml.hpp>

#include "host/util.h"

namespace sm {
namespace host {
namespace {

constexpr std::int32_t kUiStateSchemaVersion = 1;

struct LevelToken {
  const char*  token;
  std::int32_t index;
};

constexpr LevelToken kNotifyLevelTokens[] = {
    {"debug", static_cast<std::int32_t>(sm::Level::Debug)},
    {"info", static_cast<std::int32_t>(sm::Level::Info)},
    {"warning", static_cast<std::int32_t>(sm::Level::Warning)},
    {"error", static_cast<std::int32_t>(sm::Level::Error)},
    {"critical", static_cast<std::int32_t>(sm::Level::Critical)},
};

constexpr LevelToken kLogLevelTokens[] = {
    {"trace", static_cast<std::int32_t>(sm::LogLevel::Trace)},
    {"debug", static_cast<std::int32_t>(sm::LogLevel::Debug)},
    {"info", static_cast<std::int32_t>(sm::LogLevel::Info)},
    {"warn", static_cast<std::int32_t>(sm::LogLevel::Warn)},
    {"error", static_cast<std::int32_t>(sm::LogLevel::Error)},
};

bool ReadLevelMask(const toml::table& table, const char* key, const LevelToken* tokens,
                   std::size_t token_count, std::uint32_t* out_mask) {
  const toml::array* const array = table[key].as_array();
  if (array == nullptr) return false;

  std::uint32_t mask = 0;
  for (const toml::node& element : *array) {
    const std::optional<std::string> text = element.value<std::string>();
    if (!text.has_value()) return false;

    bool found = false;
    for (std::size_t i = 0; i < token_count; ++i) {
      if (*text == tokens[i].token) {
        mask |= 1u << static_cast<std::uint32_t>(tokens[i].index);
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  *out_mask = mask;
  return true;
}

std::string ReadString(const toml::table& table, const char* key,
                       const std::string& fallback) {
  const std::optional<std::string> text = table[key].value<std::string>();
  return text.has_value() ? *text : fallback;
}

bool ReadBool(const toml::table& table, const char* key, bool fallback) {
  const std::optional<bool> value = table[key].value<bool>();
  return value.has_value() ? *value : fallback;
}

std::int64_t ReadInt(const toml::table& table, const char* key, std::int64_t fallback,
                     std::int64_t minimum, std::int64_t maximum) {
  const std::optional<std::int64_t> value = table[key].value<std::int64_t>();
  if (!value.has_value()) return fallback;
  if (*value < minimum || *value > maximum) return fallback;
  return *value;
}

const toml::table* SubTable(const toml::table& table, const char* key) {
  return table[key].as_table();
}

std::string QuoteToml(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:   out.push_back(c); break;
    }
  }
  out.push_back('"');
  return out;
}

void WriteLevelArray(std::string* out, const LevelToken* tokens, std::size_t token_count,
                     std::uint32_t mask) {
  out->push_back('[');
  bool first = true;
  for (std::size_t i = 0; i < token_count; ++i) {
    const std::uint32_t bit = 1u << static_cast<std::uint32_t>(tokens[i].index);
    if ((mask & bit) == 0) continue;
    if (!first) *out += ", ";
    first = false;
    out->push_back('"');
    *out += tokens[i].token;
    out->push_back('"');
  }
  out->push_back(']');
}

void AppendInt(std::string* out, const char* key, std::int64_t value) {
  char line[64];
  std::snprintf(line, sizeof(line), "%s = %lld\n", key, static_cast<long long>(value));
  *out += line;
}

void AppendBool(std::string* out, const char* key, bool value) {
  *out += key;
  *out += value ? " = true\n" : " = false\n";
}

void AppendString(std::string* out, const char* key, const std::string& value) {
  *out += key;
  *out += " = ";
  *out += QuoteToml(value);
  *out += "\n";
}

}  // namespace

const char* PanelPageKey(PanelPage page) {
  switch (page) {
    case PanelPage::Overview:      return "overview";
    case PanelPage::Plugins:       return "plugins";
    case PanelPage::Notifications: return "notifications";
    case PanelPage::Routing:       return "routing";
    case PanelPage::Log:           return "log";
    case PanelPage::About:         return "about";
  }
  return "overview";
}

PanelPage PanelPageFromKey(const char* key, PanelPage fallback) {
  if (key == nullptr) return fallback;
  const std::string text(key);
  for (std::int32_t i = 0; i < kPanelPageCount; ++i) {
    const auto page = static_cast<PanelPage>(i);
    if (text == PanelPageKey(page)) return page;
  }
  return fallback;
}

namespace {

const char* TokenOf(const LevelToken* tokens, std::size_t count, std::int32_t index) {
  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].index == index) return tokens[i].token;
  }
  return "unknown";
}

}  // namespace

const char* LevelKey(sm::Level level) {
  return TokenOf(kNotifyLevelTokens,
                 sizeof(kNotifyLevelTokens) / sizeof(kNotifyLevelTokens[0]),
                 static_cast<std::int32_t>(level));
}

const char* LogLevelKey(sm::LogLevel level) {
  return TokenOf(kLogLevelTokens, sizeof(kLogLevelTokens) / sizeof(kLogLevelTokens[0]),
                 static_cast<std::int32_t>(level));
}

std::string UiStatePath(const config::Paths& paths) {
  return util::path_join(paths.host_dir, "ui_state.toml");
}

UiStateLoad LoadUiState(const std::string& file_path, UiState* out,
                        std::string* out_error) {
  UiState state;
  if (out == nullptr) return UiStateLoad::Missing;

  std::string text;
  if (!util::read_text_file(file_path, &text, out_error)) {
    *out = state;
    return UiStateLoad::Missing;
  }

  toml::table table;
  try {
    table = toml::parse(text, std::string_view(file_path));
  } catch (const toml::parse_error& err) {
    if (out_error != nullptr) *out_error = std::string(err.description());
    *out = state;
    return UiStateLoad::Corrupt;
  } catch (const std::exception& err) {
    if (out_error != nullptr) *out_error = err.what();
    *out = state;
    return UiStateLoad::Corrupt;
  }

  const std::int64_t schema =
      ReadInt(table, "schema_version", kUiStateSchemaVersion, 1, 1000);
  if (schema > kUiStateSchemaVersion) {
    *out = state;
    return UiStateLoad::TooNew;
  }

  if (const toml::table* const window = SubTable(table, "window")) {
    state.x = static_cast<int>(
        ReadInt(*window, "x", UiState::kUseSystemPosition, -32000, 32000));
    state.y = static_cast<int>(
        ReadInt(*window, "y", UiState::kUseSystemPosition, -32000, 32000));
    state.w = static_cast<int>(ReadInt(*window, "w", 0, 0, 20000));
    state.h = static_cast<int>(ReadInt(*window, "h", 0, 0, 20000));
  }

  if (const toml::table* const panel = SubTable(table, "panel")) {
    state.last_page = PanelPageFromKey(ReadString(*panel, "last_page", "").c_str(),
                                       PanelPage::Overview);
    state.close_hint_shown = ReadBool(*panel, "close_hint_shown", false);
  }

  if (const toml::table* const filter = SubTable(table, "notify_filter")) {
    ReadLevelMask(*filter, "levels", kNotifyLevelTokens,
                  sizeof(kNotifyLevelTokens) / sizeof(kNotifyLevelTokens[0]),
                  &state.notify.levels.mask);
    state.notify.source = ReadString(*filter, "source", "");
    state.notify.topic = ReadString(*filter, "topic", "");
    state.notify.text = ReadString(*filter, "text", "");
  }

  if (const toml::table* const filter = SubTable(table, "log_filter")) {
    ReadLevelMask(*filter, "levels", kLogLevelTokens,
                  sizeof(kLogLevelTokens) / sizeof(kLogLevelTokens[0]),
                  &state.log.levels.mask);
    state.log.source = ReadString(*filter, "source", "");
    state.log.text = ReadString(*filter, "text", "");
    state.log.follow = ReadBool(*filter, "follow", true);
    state.log.paused = ReadBool(*filter, "paused", false);
  }

  *out = std::move(state);
  return UiStateLoad::Ok;
}

bool SaveUiState(const std::string& file_path, const UiState& state,
                 std::string* out_error) {
  std::string text;
  text.reserve(512);
  text += "# SimpleMessage 的界面状态。由宿主自动维护；手工改动会在下次关闭面板时被覆盖。\n";
  AppendInt(&text, "schema_version", kUiStateSchemaVersion);
  text += "\n[window]\n";
  text += "# x/y 为 -1 表示交给系统摆放；w/h 为 0 表示用默认尺寸。\n";
  AppendInt(&text, "x", state.x);
  AppendInt(&text, "y", state.y);
  AppendInt(&text, "w", state.w);
  AppendInt(&text, "h", state.h);

  text += "\n[panel]\n";
  AppendString(&text, "last_page", PanelPageKey(state.last_page));
  AppendBool(&text, "close_hint_shown", state.close_hint_shown);

  text += "\n[notify_filter]\n";
  text += "levels = ";
  WriteLevelArray(&text, kNotifyLevelTokens,
                  sizeof(kNotifyLevelTokens) / sizeof(kNotifyLevelTokens[0]),
                  state.notify.levels.mask);
  text += "\n";
  AppendString(&text, "source", state.notify.source);
  AppendString(&text, "topic", state.notify.topic);
  AppendString(&text, "text", state.notify.text);

  text += "\n[log_filter]\n";
  text += "levels = ";
  WriteLevelArray(&text, kLogLevelTokens,
                  sizeof(kLogLevelTokens) / sizeof(kLogLevelTokens[0]),
                  state.log.levels.mask);
  text += "\n";
  AppendString(&text, "source", state.log.source);
  AppendString(&text, "text", state.log.text);
  AppendBool(&text, "follow", state.log.follow);
  AppendBool(&text, "paused", state.log.paused);

  return util::write_text_file_atomically(file_path, text, out_error);
}

}  // namespace host
}  // namespace sm

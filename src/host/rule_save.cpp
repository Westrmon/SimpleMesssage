// SimpleMessage — 规则保存写入器的实现
#include "host/rule_save.h"

#include <cstdio>
#include <ctime>
#include <utility>

#include "platform/fs.h"
#include "platform/system.h"

#include "host/log.h"
#include "host/placeholders.h"
#include "host/rules.h"
#include "host/util.h"

namespace sm {
namespace host {
namespace {

constexpr std::size_t kMaxSavedPathBytes = 900;
constexpr std::size_t kMaxWarnedPaths = 32;
constexpr const char* kDefaultPathTemplate = "notify/{ruleid}.txt";

const char* LevelToken(sm::Level level) {
  switch (level) {
    case sm::Level::Debug: return "debug";
    case sm::Level::Info: return "info";
    case sm::Level::Warning: return "warning";
    case sm::Level::Error: return "error";
    case sm::Level::Critical: return "critical";
  }
  return "info";
}

std::string TextOrEmpty(const char* text) {
  return text == nullptr ? std::string() : std::string(text);
}

std::string OneLine(const char* text) {
  std::string out = TextOrEmpty(text);
  for (char& c : out) {
    if (c == '\r' || c == '\n') c = ' ';
  }
  return out;
}

std::string OneLine(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c == '\r' || c == '\n') c = ' ';
  }
  return out;
}

bool IsBlank(const std::string& text) {
  for (const char c : text) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

bool HasDotSegment(const std::string& path) {
  std::size_t start = 0;
  while (start < path.size()) {
    std::size_t end = start;
    while (end < path.size() && path[end] != '/' && path[end] != '\\') ++end;
    if (end > start) {
      const std::string_view segment(path.data() + start, end - start);
      if (segment == "." || segment == "..") return true;
    }
    start = end + 1;
  }
  return false;
}

void AppendField(std::string* out, const char* key, const std::string& value) {
  out->append(key);
  out->append(": ");
  out->append(value);
  out->push_back('\n');
}

std::string FormatTime(std::int64_t time_unix_ms) {
  const std::time_t seconds = static_cast<std::time_t>(time_unix_ms / 1000);
  std::tm parts{};
  if (!platform::local_time(seconds, &parts)) return {};
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                parts.tm_hour, parts.tm_min, parts.tm_sec,
                static_cast<int>(time_unix_ms % 1000));
  return buffer;
}

std::string BuildBlock(const Rule& rule, const sm::Notification& view,
                       bool delivery_phase, const std::string& notifier_id,
                       std::int64_t time_ms) {
  std::string block;
  block.reserve(view.content == nullptr ? 256 : std::string(view.content).size() + 256);

  block.append("===== SimpleMessage rule save =====\n");

  std::string owner = OneLine(rule.id);
  const std::string name = OneLine(rule.name);
  if (!name.empty()) owner += " (" + name + ")";
  AppendField(&block, "rule", owner);

  AppendField(&block, "phase", delivery_phase ? "delivery" : "source");
  if (delivery_phase) AppendField(&block, "notifier", OneLine(notifier_id));
  AppendField(&block, "time", FormatTime(time_ms));
  if (view.seq > 0) AppendField(&block, "seq", std::to_string(view.seq));
  AppendField(&block, "level", LevelToken(view.level));
  AppendField(&block, "source", OneLine(view.source_plugin));
  AppendField(&block, "topic", OneLine(view.topic));
  if (view.id != nullptr && view.id[0] != '\0') {
    AppendField(&block, "id", OneLine(view.id));
  }
  AppendField(&block, "title", OneLine(view.title));
  if (view.summary != nullptr && view.summary[0] != '\0') {
    AppendField(&block, "summary", OneLine(view.summary));
  }
  if (view.url != nullptr && view.url[0] != '\0') {
    AppendField(&block, "url", OneLine(view.url));
  }
  if (view.icon != nullptr && view.icon[0] != '\0') {
    AppendField(&block, "icon", OneLine(view.icon));
  }

  if (view.actions.data != nullptr && view.actions.size != 0) {
    block.append("actions:\n");
    for (std::size_t i = 0; i < view.actions.size; ++i) {
      block.append("  - ");
      block.append(OneLine(view.actions.data[i].id));
      const std::string label = OneLine(view.actions.data[i].label);
      if (!label.empty()) block.append(" = " + label);
      block.push_back('\n');
    }
  }

  block.append("content:\n");
  if (view.content != nullptr) block.append(view.content);
  block.append("\n===== end =====\n");
  return block;
}

}  // namespace

void RuleSaveSink::SetDataDir(std::string data_dir) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_dir_ = std::move(data_dir);
}

bool RuleSaveSink::ShouldWarnLocked(const std::string& path) {
  if (warned_paths_.size() >= kMaxWarnedPaths &&
      warned_paths_.find(path) == warned_paths_.end()) {
    return false;
  }
  return warned_paths_.insert(path).second;
}

void RuleSaveSink::Save(const Rule& rule, const sm::Notification& view,
                        bool delivery_phase, const std::string& notifier_id) {
  const std::int64_t time_ms =
      view.time_unix_ms > 0 ? view.time_unix_ms : util::now_unix_ms();
  const std::time_t seconds = static_cast<std::time_t>(time_ms / 1000);
  std::tm parts{};
  if (!platform::local_time(seconds, &parts)) parts = {};

  char date[16];
  char year[8];
  char month[4];
  char day[4];
  char hour[4];
  char minute[4];
  char second[4];
  char time[16];
  char datetime[24];
  std::snprintf(date, sizeof(date), "%04d-%02d-%02d", parts.tm_year + 1900,
                parts.tm_mon + 1, parts.tm_mday);
  std::snprintf(year, sizeof(year), "%04d", parts.tm_year + 1900);
  std::snprintf(month, sizeof(month), "%02d", parts.tm_mon + 1);
  std::snprintf(day, sizeof(day), "%02d", parts.tm_mday);
  std::snprintf(hour, sizeof(hour), "%02d", parts.tm_hour);
  std::snprintf(minute, sizeof(minute), "%02d", parts.tm_min);
  std::snprintf(second, sizeof(second), "%02d", parts.tm_sec);
  std::snprintf(time, sizeof(time), "%02d-%02d-%02d", parts.tm_hour, parts.tm_min,
                parts.tm_sec);
  std::snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d_%02d-%02d-%02d",
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                parts.tm_hour, parts.tm_min, parts.tm_sec);

  const std::string timestamp = std::to_string(time_ms);
  const std::string seq =
      view.seq > 0 ? std::to_string(view.seq) : std::string();
  const std::string phase = delivery_phase ? "delivery" : "source";

  const PlaceholderResolver resolver = [&](std::string_view key,
                                           std::string* out) {
    if (key == "date") {
      *out = date;
    } else if (key == "year") {
      *out = year;
    } else if (key == "month") {
      *out = month;
    } else if (key == "day") {
      *out = day;
    } else if (key == "hour") {
      *out = hour;
    } else if (key == "minute") {
      *out = minute;
    } else if (key == "second") {
      *out = second;
    } else if (key == "time") {
      *out = time;
    } else if (key == "datetime") {
      *out = datetime;
    } else if (key == "timestamp") {
      *out = timestamp;
    } else if (key == "title") {
      *out = TextOrEmpty(view.title);
    } else if (key == "content") {
      *out = TextOrEmpty(view.content);
    } else if (key == "summary") {
      *out = TextOrEmpty(view.summary);
    } else if (key == "topic") {
      *out = TextOrEmpty(view.topic);
    } else if (key == "level") {
      *out = LevelToken(view.level);
    } else if (key == "id") {
      *out = TextOrEmpty(view.id);
    } else if (key == "url") {
      *out = TextOrEmpty(view.url);
    } else if (key == "icon") {
      *out = TextOrEmpty(view.icon);
    } else if (key == "source") {
      *out = TextOrEmpty(view.source_plugin);
    } else if (key == "seq") {
      *out = seq;
    } else if (key == "ruleid") {
      *out = rule.id;
    } else if (key == "rulename") {
      *out = rule.name;
    } else if (key == "phase") {
      *out = phase;
    } else if (key == "notifier") {
      *out = notifier_id;
    } else {
      return false;
    }
    return true;
  };

  std::string warn_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string pattern = rule.save.path;
    if (IsBlank(pattern)) pattern = kDefaultPathTemplate;

    std::string expanded =
        ExpandPlaceholders(pattern, PlaceholderMode::Path, resolver);
    std::string path;
    if (expanded.empty()) {
      path.clear();
    } else if (util::path_is_absolute(expanded)) {
      path = std::move(expanded);
    } else {
      path = util::path_join(data_dir_, expanded);
    }

    bool valid = !path.empty() && path.size() <= kMaxSavedPathBytes &&
                 !util::path_filename(path).empty() && !HasDotSegment(path);

    if (valid) {
      std::FILE* file = platform::fs::open_file(path.c_str(), "ab");
      if (file == nullptr) {
        const std::string dir = util::path_dir(path);
        if (!dir.empty() && util::ensure_directory(dir)) {
          file = platform::fs::open_file(path.c_str(), "ab");
        }
      }
      if (file == nullptr) {
        valid = false;
      } else {
        const std::string block =
            BuildBlock(rule, view, delivery_phase, notifier_id, time_ms);
        const bool written =
            std::fwrite(block.data(), 1, block.size(), file) == block.size();
        std::fflush(file);
        std::fclose(file);
        valid = written;
      }
    }

    if (!valid && ShouldWarnLocked(path.empty() ? std::string("<empty>") : path)) {
      warn_path = path.empty() ? std::string("<empty>") : path;
    }
  }

  if (!warn_path.empty()) {
    log::Writef(sm::LogLevel::Warn, "host", "规则保存失败，无法写入 %s",
                warn_path.c_str());
  }
}

}  // namespace host
}  // namespace sm

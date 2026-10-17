#include "host/util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "platform/fs.h"
#include "platform/system.h"

namespace sm {
namespace util {
namespace {

bool is_separator(char c) { return c == '/' || c == '\\'; }

}  // namespace

std::string path_join(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (name.empty()) return dir;
  if (is_separator(dir.back())) return dir + name;
  return dir + platform::path_separator() + name;
}

std::string path_dir(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return {};
  if (pos == 0) return path.substr(0, 1);
  return path.substr(0, pos);
}

std::string path_filename(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

bool path_is_absolute(const std::string& path) {
  return platform::fs::is_absolute_path(path);
}

bool ensure_directory(const std::string& utf8_dir) {
  return platform::fs::ensure_directory(utf8_dir);
}

bool file_exists(const std::string& utf8_path) {
  return platform::fs::file_exists(utf8_path);
}

bool remove_file(const std::string& utf8_path) {
  return platform::fs::remove_file(utf8_path);
}

bool rename_file(const std::string& from, const std::string& to) {
  return platform::fs::rename_file(from, to);
}

bool is_directory_writable(const std::string& utf8_dir) {
  if (utf8_dir.empty()) return false;
  const std::string probe = path_join(utf8_dir, ".sm-write-probe");
  std::FILE* file = platform::fs::open_file(probe.c_str(), "wb");
  if (file == nullptr) return false;
  std::fclose(file);
  remove_file(probe);
  return true;
}

bool read_text_file(const std::string& utf8_path, std::string* out_text,
                    std::string* out_error) {
  if (out_text == nullptr) return false;
  if (utf8_path.empty()) {
    if (out_error != nullptr) *out_error = "路径为空";
    return false;
  }

  std::FILE* file = platform::fs::open_file(utf8_path.c_str(), "rb");
  if (file == nullptr) {
    if (out_error != nullptr) *out_error = "无法打开 " + utf8_path;
    return false;
  }

  std::string text;
  char buffer[4096];
  std::size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) {
    if (out_error != nullptr) *out_error = "读取失败 " + utf8_path;
    return false;
  }

  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  if (text.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    text.erase(0, 3);
  }
  *out_text = std::move(text);
  return true;
}

bool write_text_file_atomically(const std::string& utf8_path, const std::string& text,
                                std::string* out_error) {
  if (utf8_path.empty()) {
    if (out_error != nullptr) *out_error = "路径为空";
    return false;
  }

  const std::string tmp = utf8_path + ".tmp";
  const std::string bak = utf8_path + ".bak";

  {
    std::FILE* file = platform::fs::open_file(tmp.c_str(), "wb");
    if (file == nullptr) {
      if (out_error != nullptr) *out_error = "无法创建临时文件 " + tmp;
      return false;
    }
    const std::size_t written =
        text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), file);
    const bool flushed = written == text.size() && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!flushed || !closed) {
      if (out_error != nullptr) *out_error = "写入临时文件失败 " + tmp;
      remove_file(tmp);
      return false;
    }
  }

  if (file_exists(utf8_path) && !rename_file(utf8_path, bak)) {
    if (out_error != nullptr) *out_error = "无法备份 " + utf8_path;
    remove_file(tmp);
    return false;
  }
  if (!rename_file(tmp, utf8_path)) {
    if (out_error != nullptr) *out_error = "无法替换 " + utf8_path;
    remove_file(tmp);
    return false;
  }
  return true;
}

std::vector<DirEntry> list_files(const std::string& utf8_dir,
                                 const char* name_suffix) {
  return platform::fs::list_files(utf8_dir, name_suffix);
}

std::int64_t now_unix_ms() {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
}

namespace {

std::tm LocalTime(std::int64_t unix_ms) {
  const std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000);
  std::tm parts{};
  platform::local_time(seconds, &parts);
  return parts;
}

}  // namespace

void format_short_time(std::int64_t time_unix_ms, std::int64_t now_unix_ms, char* out,
                       std::size_t capacity) {
  if (out == nullptr || capacity == 0) return;
  const std::tm when = LocalTime(time_unix_ms);
  const std::tm now = LocalTime(now_unix_ms);
  const bool same_day = when.tm_year == now.tm_year && when.tm_mon == now.tm_mon &&
                        when.tm_mday == now.tm_mday;
  std::strftime(out, capacity, same_day ? "%H:%M:%S" : "%m-%d %H:%M", &when);
}

void format_duration(std::int64_t ms, char* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0) return;
  if (ms < 0) ms = 0;

  const std::int64_t total_seconds = ms / 1000;
  const std::int64_t days = total_seconds / 86400;
  const std::int64_t hours = (total_seconds % 86400) / 3600;
  const std::int64_t minutes = (total_seconds % 3600) / 60;
  const std::int64_t seconds = total_seconds % 60;

  if (days > 0) {
    std::snprintf(out, capacity, "%lld 天 %lld 小时", static_cast<long long>(days),
                  static_cast<long long>(hours));
  } else if (hours > 0) {
    std::snprintf(out, capacity, "%lld 小时 %lld 分", static_cast<long long>(hours),
                  static_cast<long long>(minutes));
  } else if (minutes > 0) {
    std::snprintf(out, capacity, "%lld 分 %lld 秒", static_cast<long long>(minutes),
                  static_cast<long long>(seconds));
  } else {
    std::snprintf(out, capacity, "%lld 秒", static_cast<long long>(seconds));
  }
}

namespace {
thread_local char g_thread_tag[16] = "ext";
}  // namespace

const char* thread_tag() { return g_thread_tag; }

void set_thread_tag(const char* tag) {
  if (tag == nullptr) tag = "ext";
  std::snprintf(g_thread_tag, sizeof(g_thread_tag), "%s", tag);
}

}  // namespace util
}  // namespace sm

#include "host/util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "sm/summary.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace sm {
namespace util {
namespace {

#ifdef _WIN32
constexpr char kPathSeparator = '\\';
#else
constexpr char kPathSeparator = '/';
#endif

bool is_separator(char c) { return c == '/' || c == '\\'; }

std::size_t decode_utf8(const char* s, std::uint32_t* out_codepoint) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  const std::uint32_t lead = p[0];
  std::size_t len = sm::utf8_sequence_length(p[0]);
  for (std::size_t k = 1; k < len; ++k) {
    if ((p[k] & 0xC0u) != 0x80u) {
      len = k;
      break;
    }
  }

  std::uint32_t cp = lead;
  if (len == 2) {
    cp = ((lead & 0x1Fu) << 6) | (p[1] & 0x3Fu);
  } else if (len == 3) {
    cp = ((lead & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
  } else if (len == 4) {
    cp = ((lead & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
         ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
  }
  *out_codepoint = cp;
  return len;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

}  // namespace

std::string narrow(const wchar_t* w) {
  std::string out;
  if (w == nullptr) return out;

  for (std::size_t i = 0; w[i] != 0; ++i) {
    std::uint32_t cp = static_cast<std::uint32_t>(w[i]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0xD800u && cp <= 0xDBFFu) {
        const std::uint32_t lo = static_cast<std::uint32_t>(w[i + 1]);
        if (lo >= 0xDC00u && lo <= 0xDFFFu) {
          cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
          ++i;
        }
      }
    }
    append_utf8(out, cp);
  }
  return out;
}

std::wstring widen(const char* utf8) {
  std::wstring out;
  if (utf8 == nullptr) return out;

  for (std::size_t i = 0; utf8[i] != '\0';) {
    std::uint32_t cp = 0;
    i += decode_utf8(utf8 + i, &cp);

    if constexpr (sizeof(wchar_t) >= 4) {
      out.push_back(static_cast<wchar_t>(cp));
    } else {
      if (cp <= 0xFFFFu) {
        out.push_back(static_cast<wchar_t>(cp));
      } else {
        const std::uint32_t v = cp - 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
      }
    }
  }
  return out;
}

std::string path_join(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (name.empty()) return dir;
  if (is_separator(dir.back())) return dir + name;
  return dir + kPathSeparator + name;
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
  if (path.empty()) return false;
  if (is_separator(path[0])) return true;
  return path.size() >= 2 && path[1] == ':';
}

std::string exe_directory() {
#ifdef _WIN32
  std::wstring buf(512, L'\0');
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      break;
    }
    buf.resize(buf.size() * 2);
  }
  return path_dir(narrow(buf.c_str()));
#else
  return {};
#endif
}

bool ensure_directory(const std::string& utf8_dir) {
  if (utf8_dir.empty()) return false;

  const std::wstring w = widen(utf8_dir.c_str());
  if (w.empty()) return false;

  auto is_creatable = [](const std::wstring& prefix) {
    if (prefix.empty() || prefix.back() == L':') return false;
    return prefix.find_first_not_of(L"/\\") != std::wstring::npos;
  };

  std::wstring prefix;
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (is_separator(static_cast<char>(w[i])) && is_creatable(prefix)) {
#ifdef _WIN32
      if (!::CreateDirectoryW(prefix.c_str(), nullptr) &&
          ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
      }
#else
      if (::mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST) return false;
#endif
    }
    prefix.push_back(w[i]);
  }
  if (is_creatable(prefix)) {
#ifdef _WIN32
    if (!::CreateDirectoryW(prefix.c_str(), nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
      return false;
    }
#else
    if (::mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST) return false;
#endif
  }
  return true;
}

bool file_exists(const std::string& utf8_path) {
  if (utf8_path.empty()) return false;
#ifdef _WIN32
  const std::wstring w = widen(utf8_path.c_str());
  const DWORD attr = ::GetFileAttributesW(w.c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
  struct stat st {};
  return ::stat(utf8_path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool remove_file(const std::string& utf8_path) {
  if (utf8_path.empty()) return false;
#ifdef _WIN32
  const std::wstring w = widen(utf8_path.c_str());
  if (::DeleteFileW(w.c_str())) return true;
  return ::GetLastError() == ERROR_FILE_NOT_FOUND;
#else
  return ::remove(utf8_path.c_str()) == 0 || errno == ENOENT;
#endif
}

bool rename_file(const std::string& from, const std::string& to) {
  if (from.empty() || to.empty()) return false;
#ifdef _WIN32
  const std::wstring wfrom = widen(from.c_str());
  const std::wstring wto = widen(to.c_str());
  return ::MoveFileExW(wfrom.c_str(), wto.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

namespace {

std::FILE* OpenRead(const std::string& utf8_path) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (::_wfopen_s(&file, widen(utf8_path.c_str()).c_str(), L"rb") != 0) return nullptr;
  return file;
#else
  return std::fopen(utf8_path.c_str(), "rb");
#endif
}

std::FILE* OpenWrite(const std::string& utf8_path) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (::_wfopen_s(&file, widen(utf8_path.c_str()).c_str(), L"wb") != 0) return nullptr;
  return file;
#else
  return std::fopen(utf8_path.c_str(), "wb");
#endif
}

}  // namespace

bool is_directory_writable(const std::string& utf8_dir) {
  if (utf8_dir.empty()) return false;
  const std::string probe = path_join(utf8_dir, ".sm-write-probe");
  std::FILE* file = OpenWrite(probe);
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

  std::FILE* file = OpenRead(utf8_path);
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
    std::FILE* file = OpenWrite(tmp);
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
  std::vector<DirEntry> result;
  if (utf8_dir.empty()) return result;

  const std::string suffix = (name_suffix != nullptr) ? name_suffix : "";

  auto accept = [&](const char* name, std::uint64_t size) {
    const std::string n = name;
    if (!suffix.empty()) {
      if (n.size() < suffix.size()) return;
      if (n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) return;
    }
    result.push_back(DirEntry{n, size});
  };

#ifdef _WIN32
  const std::wstring wsz = widen((path_join(utf8_dir, "*")).c_str());
  WIN32_FIND_DATAW fd{};
  HANDLE h = ::FindFirstFileW(wsz.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return result;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
    const std::string name = narrow(fd.cFileName);
    const std::uint64_t size =
        (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    accept(name.c_str(), size);
  } while (::FindNextFileW(h, &fd));
  ::FindClose(h);
#else
  DIR* d = ::opendir(utf8_dir.c_str());
  if (d == nullptr) return result;
  while (struct dirent* e = ::readdir(d)) {
    if (e->d_name[0] == '.') continue;
    struct stat st {};
    if (::stat(path_join(utf8_dir, e->d_name).c_str(), &st) != 0) continue;
    if (!S_ISREG(st.st_mode)) continue;
    accept(e->d_name, static_cast<std::uint64_t>(st.st_size));
  }
  ::closedir(d);
#endif

  std::sort(result.begin(), result.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return result;
}

std::int64_t now_unix_ms() {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
}

namespace {

std::tm LocalTime(std::int64_t unix_ms) {
  std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &seconds);
#else
  localtime_r(&seconds, &parts);
#endif
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

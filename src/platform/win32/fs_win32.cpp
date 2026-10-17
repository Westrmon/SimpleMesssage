// SimpleMessage — Win32 平台层：文件系统原语

#include "platform/fs.h"

#include <algorithm>
#include <cstring>

#include <share.h>
#include <windows.h>

#include "platform/utf8.h"

namespace sm {
namespace platform {
namespace fs {
namespace {

bool is_separator(wchar_t c) { return c == L'/' || c == L'\\'; }

bool is_creatable(const std::wstring& prefix) {
  if (prefix.empty() || prefix.back() == L':') return false;
  return prefix.find_first_not_of(L"/\\") != std::wstring::npos;
}

}  // namespace

std::FILE* open_file(const char* utf8_path, const char* mode) {
  if (utf8_path == nullptr || mode == nullptr) return nullptr;

  const std::wstring path = utf8::widen(utf8_path);
  if (path.empty()) return nullptr;

  // 日志文件需要"允许别的进程读"，因此追加模式用共享打开的 _wfsopen。
  if (std::strcmp(mode, "ab") == 0) {
    return ::_wfsopen(path.c_str(), L"ab", _SH_DENYNO);
  }

  std::FILE* file = nullptr;
  if (::_wfopen_s(&file, path.c_str(), utf8::widen(mode).c_str()) != 0) {
    return nullptr;
  }
  return file;
}

bool ensure_directory(const std::string& utf8_dir) {
  if (utf8_dir.empty()) return false;

  const std::wstring w = utf8::widen(utf8_dir.c_str());
  if (w.empty()) return false;

  std::wstring prefix;
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (is_separator(w[i]) && is_creatable(prefix)) {
      if (!::CreateDirectoryW(prefix.c_str(), nullptr) &&
          ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
      }
    }
    prefix.push_back(w[i]);
  }
  if (is_creatable(prefix)) {
    if (!::CreateDirectoryW(prefix.c_str(), nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
      return false;
    }
  }
  return true;
}

bool file_exists(const std::string& utf8_path) {
  if (utf8_path.empty()) return false;
  const std::wstring w = utf8::widen(utf8_path.c_str());
  const DWORD attr = ::GetFileAttributesW(w.c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool remove_file(const std::string& utf8_path) {
  if (utf8_path.empty()) return false;
  const std::wstring w = utf8::widen(utf8_path.c_str());
  if (::DeleteFileW(w.c_str())) return true;
  return ::GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool rename_file(const std::string& from, const std::string& to) {
  if (from.empty() || to.empty()) return false;
  const std::wstring wfrom = utf8::widen(from.c_str());
  const std::wstring wto = utf8::widen(to.c_str());
  return ::MoveFileExW(wfrom.c_str(), wto.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::vector<DirEntry> list_files(const std::string& utf8_dir,
                                 const char* name_suffix) {
  std::vector<DirEntry> result;
  if (utf8_dir.empty()) return result;

  const std::string suffix = (name_suffix != nullptr) ? name_suffix : "";
  const std::string pattern = utf8_dir + "\\*";
  const std::wstring wsz = utf8::widen(pattern.c_str());

  WIN32_FIND_DATAW fd{};
  HANDLE h = ::FindFirstFileW(wsz.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return result;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
    const std::string name = utf8::narrow(fd.cFileName);
    if (!suffix.empty()) {
      if (name.size() < suffix.size()) continue;
      if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        continue;
      }
    }
    const std::uint64_t size =
        (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    result.push_back(DirEntry{name, size});
  } while (::FindNextFileW(h, &fd));
  ::FindClose(h);

  std::sort(result.begin(), result.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return result;
}

bool is_absolute_path(const std::string& utf8_path) {
  if (utf8_path.empty()) return false;
  if (utf8_path[0] == '/' || utf8_path[0] == '\\') return true;
  return utf8_path.size() >= 2 && utf8_path[1] == ':';
}

}  // namespace fs
}  // namespace platform
}  // namespace sm

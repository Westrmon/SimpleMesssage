// SimpleMessage — Win32 平台层：进程、shell、字体、时间与系统目录

#include "platform/system.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <shlobj.h>
#include <shellapi.h>
#include <windows.h>

#include "platform/utf8.h"

namespace sm {
namespace platform {
namespace {

std::string path_join(const char* dir, const char* name) {
  if (dir == nullptr || name == nullptr) return {};
  std::string out(dir);
  if (out.empty()) return out;
  if (out.back() != '\\' && out.back() != '/') out.push_back('\\');
  out += name;
  return out;
}

}  // namespace

std::string exe_directory() {
  std::wstring buffer(512, L'\0');
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buffer.data(),
                                         static_cast<DWORD>(buffer.size()));
    if (n == 0) return {};
    if (n < buffer.size()) {
      buffer.resize(n);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }

  const std::string path = utf8::narrow(buffer.c_str());
  const std::size_t pos = path.find_last_of("/\\");
  return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

std::vector<std::string> command_line_args() {
  std::vector<std::string> args;
  int argc = 0;
  LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv == nullptr) return args;

  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.push_back(utf8::narrow(argv[i]));
  }
  ::LocalFree(argv);
  return args;
}

bool shell_open(const char* utf8_target) {
  if (utf8_target == nullptr || *utf8_target == 0) return false;
  const std::wstring target = utf8::widen(utf8_target);
  const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", target.c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<std::intptr_t>(result) > 32;
}

std::string system_font_directory() {
  wchar_t buffer[MAX_PATH] = {};
  const HRESULT hr = ::SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr,
                                        SHGFP_TYPE_CURRENT, buffer);
  if (FAILED(hr) || buffer[0] == L'\0') return {};
  return utf8::narrow(buffer);
}

std::vector<std::string> preferred_font_files() {
  // Windows 自带的三种中文字体；按优先级排列。
  return {"msyh.ttc", "simhei.ttf", "simsun.ttc"};
}

bool local_time(std::time_t value, std::tm* out) {
  if (out == nullptr) return false;
  return ::localtime_s(out, &value) == 0;
}

const char* architecture_name() {
#if defined(_M_X64) || defined(__x86_64__)
  return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#else
  return "未知";
#endif
}

char path_separator() { return '\\'; }

std::string user_config_root() {
  char* base = nullptr;
  std::size_t length = 0;
  if (::_dupenv_s(&base, &length, "APPDATA") != 0 || base == nullptr) {
    return {};
  }
  const std::string root = path_join(base, "SimpleMessage");
  std::free(base);
  return root;
}

}  // namespace platform
}  // namespace sm

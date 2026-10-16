// SimpleMessage — Win32 平台层：动态库加载、可中断等待、单调时钟、旁路输出

#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#include <windows.h>

#include "host/log.h"
#include "host/util.h"

namespace sm {
namespace platform {
namespace {

template <typename Fn>
Fn resolve_symbol(HMODULE module, const char* name) {
  FARPROC proc = (module != nullptr) ? ::GetProcAddress(module, name) : nullptr;
  Fn fn = nullptr;
  static_assert(sizeof(fn) == sizeof(proc), "函数指针与 FARPROC 尺寸不一致");
  if (proc != nullptr) std::memcpy(&fn, &proc, sizeof(fn));
  return fn;
}

std::string describe_win32_error(DWORD code) {
  LPWSTR buffer = nullptr;
  const DWORD n = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

  std::string text;
  if (n != 0 && buffer != nullptr) {
    std::wstring w(buffer, n);
    while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ')) {
      w.pop_back();
    }
    text = util::narrow(w.c_str());
  } else {
    text = "系统未提供说明";
  }
  if (buffer != nullptr) ::LocalFree(buffer);

  char head[48];
  std::snprintf(head, sizeof(head), "错误 %lu", static_cast<unsigned long>(code));
  return std::string(head) + "：" + text;
}

const char* store_error(const std::string& text) {
  thread_local std::string buffer;
  buffer = text;
  return buffer.c_str();
}

struct SearchPathState {
  bool restricted = false;
};

SearchPathState& search_path_state() {
  static SearchPathState state;
  return state;
}

std::wstring resolve_full_path(const wchar_t* path) {
  if (path == nullptr) return {};

  std::wstring buffer(512, L'\0');
  for (;;) {
    const DWORD n = ::GetFullPathNameW(path, static_cast<DWORD>(buffer.size()),
                                       buffer.data(), nullptr);
    if (n == 0) return path;
    if (n < buffer.size()) {
      buffer.resize(n);
      return buffer;
    }
    buffer.resize(n);
  }
}

void ensure_search_path_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");

    using SetDefaultDllDirectoriesFn = BOOL(WINAPI*)(DWORD);
    using AddDllDirectoryFn = PVOID(WINAPI*)(PCWSTR);

    const auto set_default = resolve_symbol<SetDefaultDllDirectoriesFn>(
        kernel32, "SetDefaultDllDirectories");
    const auto add_directory =
        resolve_symbol<AddDllDirectoryFn>(kernel32, "AddDllDirectory");

    if (set_default != nullptr && add_directory != nullptr &&
        set_default(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) != FALSE) {
      const std::wstring exe_dir = util::widen(util::exe_directory().c_str());
      if (!exe_dir.empty()) add_directory(exe_dir.c_str());
      search_path_state().restricted = true;
      return;
    }

    const std::wstring exe_dir = util::widen(util::exe_directory().c_str());
    if (!exe_dir.empty()) ::SetDllDirectoryW(exe_dir.c_str());
    search_path_state().restricted = false;

    log::Write(sm::LogLevel::Warn, "platform",
               "SetDefaultDllDirectories 不可用（Win7 需要 KB2533623），"
               "已降级为 SetDllDirectoryW：DLL 搜索路径未被完全收紧。");
  });
}

class Win32Module final : public IModule {
 public:
  Win32Module(HMODULE handle, std::string path)
      : handle_(handle), path_(std::move(path)) {}

  ~Win32Module() override = default;

  void* symbol(const char* name) override {
    last_error_.clear();
    if (handle_ == nullptr || name == nullptr) return nullptr;

    FARPROC proc = ::GetProcAddress(handle_, name);
    if (proc == nullptr) {
      last_error_ = "找不到导出符号 ";
      last_error_ += name;
      return nullptr;
    }
    void* address = nullptr;
    static_assert(sizeof(address) == sizeof(proc), "指针尺寸不一致");
    std::memcpy(&address, &proc, sizeof(address));
    return address;
  }

  const char* last_error() const override { return last_error_.c_str(); }
  const char* path() const override { return path_.c_str(); }

  HMODULE handle() const { return handle_; }

 private:
  HMODULE     handle_ = nullptr;
  std::string path_;
  std::string last_error_;
};

class Win32Waiter final : public IWaiter {
 public:
  Win32Waiter() : event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

  ~Win32Waiter() override {
    if (event_ != nullptr) ::CloseHandle(event_);
  }

  bool wait(std::int64_t timeout_ms) override {
    if (timeout_ms <= 0) return consume_if_signaled();

    if (event_ == nullptr) {
      ::Sleep(static_cast<DWORD>(timeout_ms > 100 ? 100 : timeout_ms));
      return false;
    }

    const DWORD ms = (timeout_ms > 0x7FFFFFFF) ? 0x7FFFFFFFu
                                               : static_cast<DWORD>(timeout_ms);
    return ::WaitForSingleObject(event_, ms) == WAIT_OBJECT_0
               ? consume()
               : false;
  }

  void signal() override {
    if (event_ != nullptr) ::SetEvent(event_);
  }

 private:
  bool consume_if_signaled() {
    if (event_ == nullptr) return false;
    return ::WaitForSingleObject(event_, 0) == WAIT_OBJECT_0 ? consume() : false;
  }

  bool consume() {
    ::ResetEvent(event_);
    return true;
  }

  HANDLE event_ = nullptr;
};

}  // namespace

IModule* load_module(const char* utf8_path, const char** out_error) {
  if (out_error != nullptr) *out_error = nullptr;
  if (utf8_path == nullptr || *utf8_path == '\0') {
    if (out_error != nullptr) *out_error = store_error("插件路径为空");
    return nullptr;
  }

  ensure_search_path_once();

  const std::wstring wpath = resolve_full_path(util::widen(utf8_path).c_str());
  const DWORD flags =
      search_path_state().restricted ? LOAD_LIBRARY_SEARCH_DEFAULT_DIRS : 0u;

  HMODULE handle = ::LoadLibraryExW(wpath.c_str(), nullptr, flags);
  if (handle == nullptr) {
    if (out_error != nullptr) {
      *out_error = store_error("加载失败：" +
                               describe_win32_error(::GetLastError()));
    }
    return nullptr;
  }

  auto* module = new (std::nothrow) Win32Module(handle, util::narrow(wpath.c_str()));
  if (module == nullptr) {
    ::FreeLibrary(handle);
    if (out_error != nullptr) *out_error = store_error("内存不足，无法记录模块句柄");
    return nullptr;
  }
  return module;
}

void unload_module(IModule* module) {
  if (module == nullptr) return;
  auto* win32_module = static_cast<Win32Module*>(module);
  const HMODULE handle = win32_module->handle();
  delete win32_module;
  if (handle != nullptr) ::FreeLibrary(handle);
}

IWaiter* create_waiter() { return new (std::nothrow) Win32Waiter(); }

void destroy_waiter(IWaiter* waiter) { delete waiter; }

std::int64_t monotonic_ms() {
  static const double ticks_per_ms = [] {
    LARGE_INTEGER frequency{};
    if (::QueryPerformanceFrequency(&frequency) == FALSE ||
        frequency.QuadPart <= 0) {
      return 0.0;
    }
    return static_cast<double>(frequency.QuadPart) / 1000.0;
  }();

  LARGE_INTEGER counter{};
  ::QueryPerformanceCounter(&counter);
  if (ticks_per_ms <= 0.0) {
    return static_cast<std::int64_t>(::GetTickCount64());
  }
  return static_cast<std::int64_t>(static_cast<double>(counter.QuadPart) /
                                   ticks_per_ms);
}

void emit_debug_output(const char* utf8_line) {
  if (utf8_line == nullptr) return;
  const std::wstring line = util::widen(utf8_line);
  ::OutputDebugStringW(line.c_str());
}

}  // namespace platform
}  // namespace sm

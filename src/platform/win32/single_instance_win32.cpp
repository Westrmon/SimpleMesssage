// SimpleMessage — Win32 平台层：单实例锁与实例唤醒

#include "platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

#include <windows.h>

#include "platform/utf8.h"
#include "platform/win32/win32_shared.h"

namespace sm {
namespace platform {
namespace {

std::wstring instance_mutex_name(const char* utf8_identifier) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8_identifier);
  for (; *p != 0; ++p) {
    hash ^= static_cast<std::uint64_t>(*p);
    hash *= 1099511628211ull;
  }

  char name[64];
  std::snprintf(name, sizeof(name), "Local\\SimpleMessage.Instance.%016llx",
                static_cast<unsigned long long>(hash));
  return utf8::widen(name);
}

class Win32SingleInstanceLock final : public ISingleInstanceLock {
 public:
  explicit Win32SingleInstanceLock(HANDLE mutex) : mutex_(mutex) {}

  ~Win32SingleInstanceLock() override {
    if (mutex_ != nullptr) ::CloseHandle(mutex_);
  }

 private:
  HANDLE mutex_ = nullptr;
};

}  // namespace

ISingleInstanceLock* acquire_single_instance_lock(const char* utf8_identifier) {
  if (utf8_identifier == nullptr) return nullptr;

  const std::wstring name = instance_mutex_name(utf8_identifier);
  HANDLE mutex = ::CreateMutexW(nullptr, TRUE, name.c_str());
  if (mutex == nullptr) return nullptr;

  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    ::CloseHandle(mutex);
    return nullptr;
  }
  return new (std::nothrow) Win32SingleInstanceLock(mutex);
}

bool notify_existing_instance(const char*) {
  HWND window = ::FindWindowW(win32::kWakeWindowClass, nullptr);
  if (window == nullptr) return false;
  return ::PostMessageW(window, win32::kWakeMessage, 0, 0) != FALSE;
}

}  // namespace platform
}  // namespace sm

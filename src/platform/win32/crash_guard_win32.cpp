// SimpleMessage — Win32 平台层：崩溃屏障（SEH）

#include "platform/platform.h"

#include <cstdint>

#include <windows.h>

namespace sm {
namespace platform {

bool guarded_call(void (*fn)(void*), void* user, std::uint32_t* out_code) {
  if (out_code != nullptr) *out_code = 0;
  if (fn == nullptr) return true;

  bool ok = true;
  std::uint32_t code = 0;

  __try {
    fn(user);
  } __except (code = static_cast<std::uint32_t>(GetExceptionCode()),
              EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  if (out_code != nullptr) *out_code = code;
  return ok;
}

}  // namespace platform
}  // namespace sm

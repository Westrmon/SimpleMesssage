// SimpleMessage — 平台层内部的 UTF-16/UTF-32 与 UTF-8 互转
//
// 这两件事与操作系统无关（只取决于 wchar_t 的宽度），但平台实现层到处都要用；
// 放在 platform/ 下是为了让 win32 实现不再反向依赖 host/util.h。
#pragma once

#include <string>

namespace sm {
namespace platform {
namespace utf8 {

// UTF-16（Windows）或 UTF-32（POSIX）→ UTF-8。
std::string narrow(const wchar_t* utf16_or_32);

// UTF-8 → UTF-16（Windows）或 UTF-32（POSIX）。
std::wstring widen(const char* utf8);

}  // namespace utf8
}  // namespace platform
}  // namespace sm

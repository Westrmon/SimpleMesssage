// SimpleMessage — Win32 平台层内部的共享约定
#pragma once

namespace sm {
namespace platform {
namespace win32 {

inline constexpr const wchar_t* kWakeWindowClass = L"SimpleMessage.MainWindow";

inline constexpr unsigned int kWakeMessage = 0x8000u + 1u;

}  // namespace win32
}  // namespace platform
}  // namespace sm

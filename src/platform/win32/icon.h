// SimpleMessage — 应用图标：把可执行文件里的那一枚取成 shell 要的形状
#pragma once

#include "sm/notification.h"

#include <windows.h>

namespace sm {
namespace platform {
namespace win32 {

HICON LoadAppIcon(int size_px);

HICON LoadLevelIcon(const sm::Level* level, int size_px);

}  // namespace win32
}  // namespace platform
}  // namespace sm

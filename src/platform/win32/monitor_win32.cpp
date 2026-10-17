// SimpleMessage — Win32 平台层：显示器枚举
//
// 单独成文件：这里只依赖 user32，不需要拖上 GLFW 与窗口实现。

#include "platform/window.h"

#include <vector>

#include <windows.h>

#include "platform/utf8.h"

namespace sm {
namespace platform {
namespace window {
namespace {

WorkArea FromRect(const RECT& rect) {
  return WorkArea{rect.left, rect.top, rect.right - rect.left,
                  rect.bottom - rect.top};
}

bool IsUsable(const WorkArea& area) { return area.width > 0 && area.height > 0; }

std::vector<MonitorInfo>* g_sink = nullptr;

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM) {
  if (g_sink == nullptr) return FALSE;

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (::GetMonitorInfoW(monitor, &info) == 0) return TRUE;

  MonitorInfo out;
  out.device = utf8::narrow(info.szDevice);
  out.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  out.full = FromRect(info.rcMonitor);
  out.work = FromRect(info.rcWork);
  if (!IsUsable(out.work)) out.work = out.full;

  g_sink->push_back(std::move(out));
  return TRUE;
}

}  // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
  std::vector<MonitorInfo> found;
  g_sink = &found;
  ::EnumDisplayMonitors(nullptr, nullptr, &CollectMonitor, 0);
  g_sink = nullptr;
  return found;
}

}  // namespace window
}  // namespace platform
}  // namespace sm

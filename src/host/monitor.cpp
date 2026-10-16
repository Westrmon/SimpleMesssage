// SimpleMessage — 显示器的枚举与解析（Win32）
#include "host/monitor.h"

#include <windows.h>

#include "host/log.h"
#include "host/util.h"

namespace sm {
namespace host {
namespace {

platform::window::WorkArea FromRect(const RECT& rect) {
  return platform::window::WorkArea{rect.left, rect.top, rect.right - rect.left,
                                    rect.bottom - rect.top};
}

bool IsUsable(const platform::window::WorkArea& area) {
  return area.width > 0 && area.height > 0;
}

std::vector<MonitorInfo>* g_sink = nullptr;

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC , LPRECT ,
                             LPARAM ) {
  if (g_sink == nullptr) return FALSE;

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (::GetMonitorInfoW(monitor, &info) == 0) return TRUE;

  MonitorInfo out;
  out.device = util::narrow(info.szDevice);
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

platform::window::WorkArea ResolveWorkArea(const std::string& name) {
  const std::vector<MonitorInfo> monitors = EnumerateMonitors();
  const bool want_primary = name.empty() || name == "primary";

  if (!want_primary) {
    for (const MonitorInfo& monitor : monitors) {
      if (monitor.device == name) return monitor.work;
    }
    sm::log::Writef(sm::LogLevel::Warn, "window",
                    "[notify_window] monitor = \"%s\" 找不到这块显示器，改用主显示器",
                    name.c_str());
  }

  for (const MonitorInfo& monitor : monitors) {
    if (monitor.primary) return monitor.work;
  }

  return platform::window::PrimaryWorkArea();
}

}  // namespace host
}  // namespace sm

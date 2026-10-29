// SimpleMessage — 显示器名字解析（枚举本身在平台层）
#include "host/monitor.h"

#include <vector>

#include "host/log.h"

namespace sm {
namespace host {

platform::window::WorkArea ResolveWorkArea(const std::string& name) {
  const std::vector<MonitorInfo> monitors = EnumerateMonitors();
  const bool want_primary = name.empty() || name == "primary";

  if (!want_primary) {
    for (const MonitorInfo& monitor : monitors) {
      if (monitor.device == name) return monitor.work;
    }
    sm::log::Writef(sm::LogLevel::Warn, "window",
                    "[notify_window] monitor = \"%s\" 显示器无效，改用主显示器",
                    name.c_str());
  }

  for (const MonitorInfo& monitor : monitors) {
    if (monitor.primary) return monitor.work;
  }

  return platform::window::PrimaryWorkArea();
}

}  // namespace host
}  // namespace sm

// SimpleMessage — 显示器：把配置里的名字解析成一块屏幕的工作区
#pragma once

#include <string>
#include <vector>

#include "platform/window.h"

namespace sm {
namespace host {

struct MonitorInfo {
  std::string device;
  bool primary = false;
  platform::window::WorkArea full{};
  platform::window::WorkArea work{};
};

std::vector<MonitorInfo> EnumerateMonitors();

platform::window::WorkArea ResolveWorkArea(const std::string& name);

}  // namespace host
}  // namespace sm

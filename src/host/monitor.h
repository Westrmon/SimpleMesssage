// SimpleMessage — 显示器：把配置里的名字解析成一块屏幕的工作区
#pragma once

#include <string>

#include "platform/window.h"

namespace sm {
namespace host {

using MonitorInfo = platform::window::MonitorInfo;
using platform::window::EnumerateMonitors;

platform::window::WorkArea ResolveWorkArea(const std::string& name);

}  // namespace host
}  // namespace sm

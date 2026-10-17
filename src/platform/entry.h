// SimpleMessage — 平台层：进程入口注册
//
// 每个平台提供自己的入口符号（Windows 是 WinMain，POSIX 是 main），
// 宿主只把"真正的工作"注册进来。这样宿主代码里没有 main/WinMain，
// 也不会在 Windows 上多定义出一个 main()。
#pragma once

#include <string>
#include <vector>

namespace sm {
namespace platform {

using EntryFunction = int (*)(const std::vector<std::string>& args);

inline EntryFunction& EntrySlot() {
  static EntryFunction entry = nullptr;
  return entry;
}

inline void SetEntryFunction(EntryFunction entry) { EntrySlot() = entry; }

}  // namespace platform
}  // namespace sm

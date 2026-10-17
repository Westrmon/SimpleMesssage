// SimpleMessage SDK — 版本宏
#pragma once

#include <cstdint>

// 宿主版本由 CMake 从 git tag 推导后注入（见 cmake/SmVersion.cmake）。
// 下面的值只是不经 CMake、直接编译头文件时的兜底，不要在这里手改发布版本。
#ifndef SM_VERSION_MAJOR
#define SM_VERSION_MAJOR 0
#endif
#ifndef SM_VERSION_MINOR
#define SM_VERSION_MINOR 1
#endif
#ifndef SM_VERSION_PATCH
#define SM_VERSION_PATCH 0
#endif
#ifndef SM_VERSION_STRING
#define SM_VERSION_STRING "0.1.0"
#endif
// git describe 的完整结果（如 "v0.1.0-3-gabc1234-dirty"），仅用于展示与排障。
#ifndef SM_VERSION_DETAIL
#define SM_VERSION_DETAIL SM_VERSION_STRING
#endif

// 接口与 ABI 版本；递增规则见 docs/插件开发/02-SDK接口参考.md §1.1。
// 2：Notification 增加 icon 字段，IHostContext 增加 icon_texture()。
#define SM_API_VERSION 2

namespace sm {
inline constexpr std::int32_t kApiVersion = SM_API_VERSION;
}  // namespace sm

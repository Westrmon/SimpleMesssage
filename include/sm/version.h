// SimpleMessage SDK — 版本宏
#pragma once

#include <cstdint>

#define SM_VERSION_MAJOR 0
#define SM_VERSION_MINOR 1
#define SM_VERSION_PATCH 0
#define SM_VERSION_STRING "0.1.0"

// 接口与 ABI 版本；递增规则见 docs/插件开发/02-SDK接口参考.md §1.1。
#define SM_API_VERSION 1

namespace sm {
inline constexpr std::int32_t kApiVersion = SM_API_VERSION;
}  // namespace sm

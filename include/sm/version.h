// SimpleMessage SDK — 版本宏
#pragma once

#include <cstdint>

#ifndef SM_VERSION_MAJOR
#define SM_VERSION_MAJOR 0
#endif
#ifndef SM_VERSION_MINOR
#define SM_VERSION_MINOR 0
#endif
#ifndef SM_VERSION_PATCH
#define SM_VERSION_PATCH 0
#endif
#ifndef SM_VERSION_STRING
#define SM_VERSION_STRING "0.0.0"
#endif
#ifndef SM_VERSION_DETAIL
#define SM_VERSION_DETAIL SM_VERSION_STRING
#endif

#define SM_API_VERSION 2

namespace sm {
inline constexpr std::int32_t kApiVersion = SM_API_VERSION;
}  // namespace sm

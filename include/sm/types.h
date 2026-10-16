// SimpleMessage SDK — 基础类型、枚举与限制常量
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sm {

// Span — 跨边界的只读数组视图，等价于"指向 arena 内一段连续数据的 (指针, 长度)"。
template <typename T>
struct Span {
  const T* data = nullptr;
  std::size_t size = 0;

  constexpr Span() = default;
  constexpr Span(const T* d, std::size_t n) : data(d), size(n) {}

  constexpr const T* begin() const { return data; }
  constexpr const T* end() const { return data + size; }
  constexpr bool empty() const { return size == 0; }
  constexpr std::size_t length() const { return size; }
  constexpr const T& operator[](std::size_t i) const { return data[i]; }
};

// 由静态数组构造 Span，供插件元信息里的静态表使用
//（元素类型的规则见 docs/插件开发/02-SDK接口参考.md §3.2）。
namespace detail {

template <typename T>
struct span_element {
  using type = const T;
};

// 元素为指针时保持原样，否则加上 const。
template <typename T>
struct span_element<T*> {
  using type = T*;
};

}  // namespace detail

template <typename T, std::size_t N>
constexpr Span<typename detail::span_element<std::remove_cv_t<T>>::type> span_of(
    const T (&arr)[N]) {
  using E = typename detail::span_element<std::remove_cv_t<T>>::type;
  return Span<E>{arr, N};
}

// 限制常量：资源受限是明确的设计约束，所有可变长度数据都有硬上限。
namespace limits {

inline constexpr std::size_t kMaxIdBytes = 64;
inline constexpr std::size_t kMaxTitleBytes = 256;
inline constexpr std::size_t kMaxContentBytes = 64 * 1024;
inline constexpr std::size_t kMaxUrlBytes = 2048;
inline constexpr std::size_t kMaxTopicBytes = 128;
inline constexpr int kSummaryCodepoints = 20;
inline constexpr std::size_t kMaxActionsPerNotification = 4;

inline constexpr std::size_t kMaxActionIdBytes = 64;
inline constexpr std::size_t kMaxActionLabelBytes = 64;

inline constexpr std::size_t kMaxPluginIdBytes = 64;
inline constexpr std::size_t kMaxPluginNameBytes = 128;
inline constexpr std::size_t kMaxPluginAuthorBytes = 128;
inline constexpr std::size_t kMaxPluginVersionBytes = 32;
inline constexpr std::size_t kMaxPluginDescriptionBytes = 1024;
inline constexpr std::size_t kMaxHostRangeBytes = 256;
inline constexpr std::size_t kMaxSubscriptions = 16;

inline constexpr std::size_t kMaxSettingItems = 64;
inline constexpr std::size_t kMaxSettingNameBytes = 64;
inline constexpr std::size_t kMaxSettingLabelBytes = 128;
inline constexpr std::size_t kMaxSettingDescBytes = 512;
inline constexpr std::size_t kMaxStringSettingBytes = 1024;
inline constexpr std::size_t kMaxEnumOptions = 16;

inline constexpr std::size_t kNotificationQueueCapacity = 1000;
inline constexpr std::size_t kNotificationHistoryCapacity = 200;

}  // namespace limits

}  // namespace sm

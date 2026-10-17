// SimpleMessage — 插件类型 id / 实例 id 的纯函数
//
// 宿主把“插件类型”（一个 DLL）和“运行实例”分开：类型 id 仍由插件的 meta.id
// 决定，实例 id 由宿主生成。首个实例沿用类型 id，之后是 `<类型id>@2`、
// `<类型id>@3`……。@ 保留给宿主，插件 id 自身不允许出现。
//
// 这个头文件刻意只依赖标准库：rules.cpp 与单元测试都要在不链接宿主其余部分
// 的情况下使用它。
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace sm {
namespace host {

inline constexpr std::size_t kMaxPluginTypeIdBytes = 64;
inline constexpr std::size_t kMaxInstanceIdBytes    = 80;
inline constexpr std::size_t kMaxInstanceNameBytes  = 96;
inline constexpr std::size_t kMaxInstancesPerType   = 16;
inline constexpr std::size_t kMaxInstancesTotal     = 64;

inline bool IsValidPluginTypeIdChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
         c == '-';
}

// 与插件管理器对 meta.id 的校验保持一致：a-z 0-9 _ . -，且不使用保留前缀 sm.
inline bool IsValidPluginTypeId(std::string_view id) {
  if (id.empty() || id.size() > kMaxPluginTypeIdBytes) return false;
  if (id.size() >= 3 && id[0] == 's' && id[1] == 'm' && id[2] == '.') return false;
  for (char c : id) {
    if (!IsValidPluginTypeIdChar(c)) return false;
  }
  return true;
}

// 解析 @N 后缀。只有 N >= 2、全为十进制数字且无前导零才算合法。
// 返回值 0 表示不是“类型 id 的第 N 个实例”这种形式。
inline int ParseInstanceOrdinal(std::string_view instance_id) {
  const std::size_t at = instance_id.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= instance_id.size()) return 0;

  const std::string_view digits = instance_id.substr(at + 1);
  if (digits.size() > 3) return 0;
  if (digits[0] == '0') return 0;
  int value = 0;
  for (char c : digits) {
    if (c < '0' || c > '9') return 0;
    value = value * 10 + (c - '0');
  }
  return (value >= 2 && value <= static_cast<int>(kMaxInstancesPerType)) ? value : 0;
}

inline bool IsValidInstanceId(std::string_view id) {
  if (id.empty() || id.size() > kMaxInstanceIdBytes) return false;
  const std::size_t at = id.rfind('@');
  if (at == std::string_view::npos) return IsValidPluginTypeId(id);
  if (!IsValidPluginTypeId(id.substr(0, at))) return false;
  return ParseInstanceOrdinal(id) >= 2;
}

// `source.email@3` -> `source.email`；没有合法 @N 后缀时原样返回。
inline std::string PluginBaseId(std::string_view instance_id) {
  const int ordinal = ParseInstanceOrdinal(instance_id);
  if (ordinal < 2) return std::string(instance_id);
  return std::string(instance_id.substr(0, instance_id.rfind('@')));
}

inline bool InstanceBelongsToType(std::string_view instance_id,
                                  std::string_view type_id) {
  if (instance_id == type_id) return true;
  if (type_id.empty() || instance_id.size() <= type_id.size()) return false;
  if (instance_id[type_id.size()] != '@') return false;
  if (instance_id.substr(0, type_id.size()) != type_id) return false;
  return ParseInstanceOrdinal(instance_id) >= 2;
}

// ordinal == 1 返回裸类型 id；ordinal >= 2 返回 `<type>@<ordinal>`。
inline std::string MakeInstanceId(std::string_view type_id, int ordinal) {
  if (ordinal <= 1) return std::string(type_id);
  return std::string(type_id) + "@" + std::to_string(ordinal);
}

// 返回 instance_id 是 type_id 的第几个实例；不匹配返回 0。
inline int InstanceOrdinal(std::string_view instance_id, std::string_view type_id) {
  if (!InstanceBelongsToType(instance_id, type_id)) return 0;
  if (instance_id == type_id) return 1;
  return ParseInstanceOrdinal(instance_id);
}

// 没有该类型的实例时返回裸类型 id；否则返回最小的空闲 @2..@16。满了返回空串。
inline std::string NextInstanceId(std::string_view type_id,
                                  const std::set<std::string>& used) {
  if (used.find(std::string(type_id)) == used.end()) return std::string(type_id);
  for (std::size_t ordinal = 2; ordinal <= kMaxInstancesPerType; ++ordinal) {
    std::string candidate = MakeInstanceId(type_id, static_cast<int>(ordinal));
    if (used.find(candidate) == used.end()) return candidate;
  }
  return {};
}

}  // namespace host
}  // namespace sm

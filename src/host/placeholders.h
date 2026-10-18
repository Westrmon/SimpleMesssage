// SimpleMessage — 通用 {name} 占位符展开与路径清洗
//
// 这个模块刻意只依赖标准库：规则解析器要用它做加载期校验，测试也直接编译它。
// 具体占位符取什么值由调用方通过 resolver 提供，方便以后复用到通知以外的场景。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sm {
namespace host {

enum class PlaceholderMode : std::int32_t {
  Text = 0,  // 替换值原样写入
  Path = 1,  // 替换值按文件名规则清洗后再写入
};

using PlaceholderResolver =
    std::function<bool(std::string_view name, std::string* out_value)>;

// 展开 {name}。{{ 与 }} 分别表示字面花括号。
// 未知占位符原样保留；out_unknown 非空时按首次出现顺序记录（自动去重）。
std::string ExpandPlaceholders(std::string_view input, PlaceholderMode mode,
                               const PlaceholderResolver& resolver,
                               std::vector<std::string>* out_unknown = nullptr);

// 只扫描不展开，用于 rules.toml 的加载期校验。返回值按首次出现顺序、已去重。
std::vector<std::string> FindPlaceholderNames(std::string_view input);

// 规则保存目前支持的占位符名字（不含花括号）。
bool IsKnownNotificationPlaceholder(std::string_view name);

// 把单个占位符值清洗成可安全放进路径的一段：
// 非法字符与控制字符换成 '_'，去掉尾部点/空格，规避 Windows 保留设备名，
// 空值变成 '_'，UTF-8 安全截断到 120 字节。
std::string SanitizePathValue(std::string_view value);

}  // namespace host
}  // namespace sm

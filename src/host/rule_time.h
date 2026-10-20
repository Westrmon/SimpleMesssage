// SimpleMessage — 规则 time 重写用的字符串时间解析
//
// 只接受中国习惯的“年在前”格式（年/月/日），并兼容通知正文里常见的
// 变体：年月日汉字、斜杠/短横线/点分隔、可选的时分秒、毫秒、ISO 时区，
// 以及纯 Unix 秒 / 毫秒时间戳。解析结果总是本地时区（带显式时区时按
// 该时区换算）。
#pragma once

#include <cstdint>
#include <string_view>

namespace sm {
namespace host {

// 解析成功返回 true，并把 Unix 毫秒写入 out_unix_ms（>= 0）。
// 无法识别、日期越界或时区非法时返回 false，且不修改 out_unix_ms。
bool ParseNotificationTime(std::string_view text, std::int64_t* out_unix_ms);

}  // namespace host
}  // namespace sm

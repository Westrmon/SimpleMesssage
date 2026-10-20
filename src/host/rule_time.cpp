// SimpleMessage — 规则 time 重写用的字符串时间解析
#include "host/rule_time.h"

#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>

namespace sm {
namespace host {
namespace {

constexpr std::int64_t kMaxUnixMs = 253402300799999;  // 9999-12-31 23:59:59.999

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

void SkipSpaces(std::string_view text, std::size_t* pos) {
  while (*pos < text.size()) {
    const char c = text[*pos];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    ++*pos;
  }
}

bool ConsumeToken(std::string_view text, std::size_t* pos,
                  std::string_view token) {
  if (text.substr(*pos, token.size()) != token) return false;
  *pos += token.size();
  return true;
}

// 把 [pos, pos+count) 读成无符号整数。数字不足 min_digits 或超过
// max_digits 时失败；成功时 pos 停在最后一个数字之后。
bool ReadNumber(std::string_view text, std::size_t* pos, int min_digits,
                int max_digits, int* out) {
  const std::size_t start = *pos;
  std::size_t end = start;
  while (end < text.size() && IsDigit(text[end]) &&
         static_cast<int>(end - start) < max_digits) {
    ++end;
  }
  if (static_cast<int>(end - start) < min_digits) return false;

  int value = 0;
  for (std::size_t i = start; i < end; ++i) {
    value = value * 10 + static_cast<int>(text[i] - '0');
  }
  *out = value;
  *pos = end;
  return true;
}

bool IsLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && IsLeapYear(year)) return 29;
  return kDays[month - 1];
}

bool ValidDate(int year, int month, int day) {
  if (year < 1970 || year > 9999) return false;
  return day >= 1 && day <= DaysInMonth(year, month);
}

bool ValidClock(int hour, int minute, int second) {
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 &&
         second <= 59;
}

// Howard Hinnant 的 days_from_civil：1970-01-01 到该日期的天数。
std::int64_t DaysFromCivil(int year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = static_cast<unsigned>(
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(doe) - 719468;
}

bool MakeLocalMs(int year, int month, int day, int hour, int minute, int second,
                 int millisecond, std::int64_t* out) {
  std::tm parts{};
  parts.tm_year = year - 1900;
  parts.tm_mon = month - 1;
  parts.tm_mday = day;
  parts.tm_hour = hour;
  parts.tm_min = minute;
  parts.tm_sec = second;
  parts.tm_isdst = -1;

  const std::time_t seconds = std::mktime(&parts);
  if (seconds == static_cast<std::time_t>(-1)) return false;
  const std::int64_t ms =
      static_cast<std::int64_t>(seconds) * 1000 + millisecond;
  if (ms <= 0 || ms > kMaxUnixMs) return false;
  *out = ms;
  return true;
}

bool MakeUtcMs(int year, int month, int day, int hour, int minute, int second,
               int millisecond, int offset_seconds, std::int64_t* out) {
  const std::int64_t days = DaysFromCivil(year, month, day);
  const std::int64_t seconds =
      days * 86400 + hour * 3600 + minute * 60 + second - offset_seconds;
  if (seconds <= 0) return false;
  const std::int64_t ms = seconds * 1000 + millisecond;
  if (ms > kMaxUnixMs) return false;
  *out = ms;
  return true;
}

bool ParseCompactDigits(std::int64_t value, std::size_t digits,
                        std::int64_t* out) {
  const auto digit = [&](std::size_t index) -> int {
    // index 从最高位开始；本函数只在 digits <= 17 时调用。
    std::int64_t divisor = 1;
    for (std::size_t i = index + 1; i < digits; ++i) divisor *= 10;
    return static_cast<int>((value / divisor) % 10);
  };
  const auto number = [&](std::size_t start, std::size_t count) -> int {
    int result = 0;
    for (std::size_t i = 0; i < count; ++i) result = result * 10 + digit(start + i);
    return result;
  };

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;

  if (digits == 8) {
    year = number(0, 4);
    month = number(4, 2);
    day = number(6, 2);
  } else if (digits == 12) {
    year = number(0, 4);
    month = number(4, 2);
    day = number(6, 2);
    hour = number(8, 2);
    minute = number(10, 2);
  } else if (digits == 14) {
    year = number(0, 4);
    month = number(4, 2);
    day = number(6, 2);
    hour = number(8, 2);
    minute = number(10, 2);
    second = number(12, 2);
  } else if (digits == 17) {
    year = number(0, 4);
    month = number(4, 2);
    day = number(6, 2);
    hour = number(8, 2);
    minute = number(10, 2);
    second = number(12, 2);
    millisecond = number(14, 3);
  } else {
    return false;
  }

  if (!ValidDate(year, month, day) || !ValidClock(hour, minute, second)) {
    return false;
  }
  return MakeLocalMs(year, month, day, hour, minute, second, millisecond, out);
}

bool ParseAllDigits(std::string_view text, std::int64_t* out) {
  if (text.empty()) return false;
  for (const char c : text) {
    if (!IsDigit(c)) return false;
  }

  std::int64_t value = 0;
  for (const char c : text) {
    value = value * 10 + static_cast<std::int64_t>(c - '0');
  }

  // 8 / 14 / 17 位没有歧义：只可能是紧凑日期。
  if (text.size() == 8 || text.size() == 14 || text.size() == 17) {
    return ParseCompactDigits(value, text.size(), out);
  }

  // 12 位既可能是 YYYYMMDDHHMM，也可能是 1973–2001 年的 Unix 毫秒。
  // 先按紧凑日期解释；解释不通再退回 Unix 毫秒。
  if (text.size() == 12 && ParseCompactDigits(value, text.size(), out)) {
    return true;
  }

  std::int64_t ms = 0;
  if (text.size() == 9 || text.size() == 10) {
    ms = value * 1000;  // Unix 秒
  } else if (text.size() >= 11 && text.size() <= 13) {
    ms = value;  // Unix 毫秒
  } else {
    return false;
  }

  if (ms <= 0 || ms > kMaxUnixMs) return false;
  *out = ms;
  return true;
}

// 年/月/日之间的分隔符：- / . 或汉字，或者仅靠空白分开。
bool ConsumeDateSeparator(std::string_view text, std::size_t* pos) {
  SkipSpaces(text, pos);
  if (*pos >= text.size()) return false;
  const char c = text[*pos];
  if (c == '-' || c == '/' || c == '.') {
    ++*pos;
    SkipSpaces(text, pos);
    return true;
  }
  if (ConsumeToken(text, pos, "年") || ConsumeToken(text, pos, "月")) {
    SkipSpaces(text, pos);
    return true;
  }
  // “2026 10 20”：空白本身就是分隔符。
  return IsDigit(c);
}

bool ConsumeHourSeparator(std::string_view text, std::size_t* pos) {
  if (*pos < text.size() && text[*pos] == ':') {
    ++*pos;
    return true;
  }
  return ConsumeToken(text, pos, "时") || ConsumeToken(text, pos, "点");
}

bool ConsumeMinuteSeparator(std::string_view text, std::size_t* pos) {
  if (*pos < text.size() && text[*pos] == ':') {
    ++*pos;
    return true;
  }
  return ConsumeToken(text, pos, "分");
}

bool EqualsIgnoreCase(std::string_view text, std::size_t pos,
                      std::string_view token) {
  if (text.size() - pos < token.size()) return false;
  for (std::size_t i = 0; i < token.size(); ++i) {
    char a = text[pos + i];
    char b = token[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

// 返回 true 表示识别到了时区（offset_seconds 已填好）。
bool ParseTimeZone(std::string_view text, std::size_t* pos,
                   int* offset_seconds) {
  *offset_seconds = 0;
  const std::size_t start = *pos;
  if (*pos >= text.size()) return false;

  if (text[*pos] == 'Z' || text[*pos] == 'z') {
    ++*pos;
    return true;
  }

  // “北京时间”等价于 UTC+8。
  if (ConsumeToken(text, pos, "北京时间")) {
    *offset_seconds = 8 * 3600;
    return true;
  }

  bool saw_name = false;
  if (EqualsIgnoreCase(text, *pos, "UTC") ||
      EqualsIgnoreCase(text, *pos, "GMT")) {
    *pos += 3;
    saw_name = true;
  }

  if (*pos < text.size() && (text[*pos] == '+' || text[*pos] == '-')) {
    const int sign = (text[*pos] == '-') ? -1 : 1;
    ++*pos;

    int hour = 0;
    int minute = 0;
    if (!ReadNumber(text, pos, 1, 2, &hour)) {
      *pos = start;
      return false;
    }
    if (*pos < text.size() && text[*pos] == ':') {
      ++*pos;
      if (!ReadNumber(text, pos, 2, 2, &minute)) {
        *pos = start;
        return false;
      }
    } else if (*pos < text.size() && IsDigit(text[*pos])) {
      if (!ReadNumber(text, pos, 2, 2, &minute)) {
        *pos = start;
        return false;
      }
    }
    if (hour > 23 || minute > 59) {
      *pos = start;
      return false;
    }
    *offset_seconds = sign * (hour * 3600 + minute * 60);
    return true;
  }

  // 只写了 “UTC” / “GMT” 而没有偏移量：按 UTC 处理。
  if (saw_name) return true;
  *pos = start;
  return false;
}

bool ParseDateTime(std::string_view text, std::int64_t* out) {
  std::size_t pos = 0;
  SkipSpaces(text, &pos);

  int year = 0;
  if (!ReadNumber(text, &pos, 4, 4, &year)) return false;
  if (!ConsumeDateSeparator(text, &pos)) return false;

  int month = 0;
  if (!ReadNumber(text, &pos, 1, 2, &month)) return false;
  if (!ConsumeDateSeparator(text, &pos)) return false;

  int day = 0;
  if (!ReadNumber(text, &pos, 1, 2, &day)) return false;
  if (!ValidDate(year, month, day)) return false;

  SkipSpaces(text, &pos);
  ConsumeToken(text, &pos, "日");
  SkipSpaces(text, &pos);

  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;
  bool has_time = false;

  if (pos < text.size()) {
    if (text[pos] == 'T' || text[pos] == 't') {
      ++pos;
      SkipSpaces(text, &pos);
    }
    if (!ReadNumber(text, &pos, 1, 2, &hour)) return false;
    if (!ConsumeHourSeparator(text, &pos)) return false;
    if (!ReadNumber(text, &pos, 1, 2, &minute)) return false;
    if (!ValidClock(hour, minute, 0)) return false;

    if (ConsumeMinuteSeparator(text, &pos)) {
      if (!ReadNumber(text, &pos, 1, 2, &second)) return false;
      if (!ValidClock(hour, minute, second)) return false;

      if (pos < text.size() && (text[pos] == '.' || text[pos] == ',')) {
        ++pos;
        const std::size_t start = pos;
        while (pos < text.size() && IsDigit(text[pos]) && pos - start < 9) {
          ++pos;
        }
        if (pos == start) return false;
        int scale = 100;
        for (std::size_t i = start; i < pos && scale > 0; ++i, scale /= 10) {
          millisecond += static_cast<int>(text[i] - '0') * scale;
        }
      }
      ConsumeToken(text, &pos, "秒");
    }
    has_time = true;
  }

  SkipSpaces(text, &pos);
  int offset_seconds = 0;
  const bool has_zone = ParseTimeZone(text, &pos, &offset_seconds);
  SkipSpaces(text, &pos);
  if (pos != text.size()) return false;

  if (has_zone) {
    return MakeUtcMs(year, month, day, hour, minute, second, millisecond,
                     offset_seconds, out);
  }
  return MakeLocalMs(year, month, day, hour, minute, second, millisecond, out);
}

}  // namespace

bool ParseNotificationTime(std::string_view text, std::int64_t* out_unix_ms) {
  if (out_unix_ms == nullptr) return false;

  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                         text[begin] == '\r' || text[begin] == '\n')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  text = text.substr(begin, end - begin);
  if (text.empty()) return false;

  std::int64_t value = 0;
  if (!ParseAllDigits(text, &value) && !ParseDateTime(text, &value)) {
    return false;
  }
  *out_unix_ms = value;
  return true;
}

}  // namespace host
}  // namespace sm

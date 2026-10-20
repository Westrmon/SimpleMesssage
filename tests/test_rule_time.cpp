// SimpleMessage — rules.rewrite.time 的字符串时间解析测试
#include "test_framework.h"

#include <cstdint>
#include <ctime>
#include <string>

#include "host/rule_time.h"

namespace {

bool Parse(const std::string& text, std::int64_t* out) {
  return sm::host::ParseNotificationTime(text, out);
}

// 用本机本地时区构造期望值；解析器对无时区的输入也走同一条路径。
std::int64_t LocalMs(int year, int month, int day, int hour, int minute,
                     int second, int millisecond = 0) {
  std::tm parts{};
  parts.tm_year = year - 1900;
  parts.tm_mon = month - 1;
  parts.tm_mday = day;
  parts.tm_hour = hour;
  parts.tm_min = minute;
  parts.tm_sec = second;
  parts.tm_isdst = -1;
  return static_cast<std::int64_t>(std::mktime(&parts)) * 1000 + millisecond;
}

}  // namespace

SM_TEST(rule_time, chinese_years_first_formats_are_equivalent) {
  std::int64_t slash = 0;
  std::int64_t dash = 0;
  std::int64_t chinese = 0;
  std::int64_t chinese_clock = 0;
  std::int64_t spaces = 0;
  SM_CHECK(Parse("2026/10/20 12:34:56", &slash));
  SM_CHECK(Parse("2026-10-20 12:34:56", &dash));
  SM_CHECK(Parse("2026年10月20日 12:34:56", &chinese));
  SM_CHECK(Parse("2026年10月20日12时34分56秒", &chinese_clock));
  SM_CHECK(Parse("2026 10 20 12:34:56", &spaces));

  const std::int64_t expected = LocalMs(2026, 10, 20, 12, 34, 56);
  SM_CHECK_INT(slash, expected);
  SM_CHECK_INT(dash, expected);
  SM_CHECK_INT(chinese, expected);
  SM_CHECK_INT(chinese_clock, expected);
  SM_CHECK_INT(spaces, expected);
}

SM_TEST(rule_time, optional_seconds_and_milliseconds) {
  std::int64_t with_seconds = 0;
  std::int64_t without_seconds = 0;
  std::int64_t with_millis = 0;
  SM_CHECK(Parse("2026/10/20 12:34:56", &with_seconds));
  SM_CHECK(Parse("2026/10/20 12:34", &without_seconds));
  SM_CHECK(Parse("2026/10/20 12:34:56.789", &with_millis));

  SM_CHECK_INT(without_seconds, LocalMs(2026, 10, 20, 12, 34, 0));
  SM_CHECK_INT(with_millis, LocalMs(2026, 10, 20, 12, 34, 56, 789));
  SM_CHECK_INT(with_millis - with_seconds, 789);
}

SM_TEST(rule_time, iso_zone_and_offset_are_utc_based) {
  std::int64_t zulu = 0;
  std::int64_t plus8 = 0;
  std::int64_t compact_offset = 0;
  std::int64_t chinese_zone = 0;
  SM_CHECK(Parse("2000-01-01T00:00:00Z", &zulu));
  SM_CHECK(Parse("2000-01-01T08:00:00+08:00", &plus8));
  SM_CHECK(Parse("2000-01-01 08:00:00 +0800", &compact_offset));
  SM_CHECK(Parse("2000-01-01 08:00 北京时间", &chinese_zone));

  SM_CHECK_INT(zulu, 946684800000);
  SM_CHECK_INT(plus8, zulu);
  SM_CHECK_INT(compact_offset, zulu);
  SM_CHECK_INT(chinese_zone, zulu);
}

SM_TEST(rule_time, compact_and_unix_timestamps) {
  std::int64_t compact_date = 0;
  std::int64_t compact_minute = 0;
  std::int64_t compact_second = 0;
  std::int64_t compact_millis = 0;
  std::int64_t unix_seconds = 0;
  std::int64_t unix_millis = 0;

  SM_CHECK(Parse("20000101", &compact_date));
  SM_CHECK(Parse("200001010000", &compact_minute));
  SM_CHECK(Parse("20000101000000", &compact_second));
  SM_CHECK(Parse("20000101000000789", &compact_millis));
  SM_CHECK(Parse("946684800", &unix_seconds));
  SM_CHECK(Parse("946684800000", &unix_millis));

  SM_CHECK_INT(compact_date, LocalMs(2000, 1, 1, 0, 0, 0));
  SM_CHECK_INT(compact_minute, LocalMs(2000, 1, 1, 0, 0, 0));
  SM_CHECK_INT(compact_second, LocalMs(2000, 1, 1, 0, 0, 0));
  SM_CHECK_INT(compact_millis, LocalMs(2000, 1, 1, 0, 0, 0, 789));
  SM_CHECK_INT(unix_seconds, 946684800000);
  SM_CHECK_INT(unix_millis, 946684800000);
}

SM_TEST(rule_time, rejects_day_first_invalid_and_empty) {
  std::int64_t out = 123;
  SM_CHECK(!Parse("20/10/2026 12:34:56", &out));  // 日/月/年不支持
  SM_CHECK_INT(out, 123);                          // 失败时不改输出
  SM_CHECK(!Parse("2026/13/20", &out));
  SM_CHECK(!Parse("2026-02-30", &out));
  SM_CHECK(!Parse("2025-02-29", &out));
  SM_CHECK(!Parse("2026-10-20 25:00", &out));
  SM_CHECK(!Parse("2026-10-20T12:34:56+25:00", &out));
  SM_CHECK(!Parse("", &out));
  SM_CHECK(!Parse("   ", &out));
  SM_CHECK(!Parse("not a time", &out));
}

SM_TEST(rule_time, leap_day_is_accepted) {
  std::int64_t value = 0;
  SM_CHECK(Parse("2024-02-29 00:00", &value));
  SM_CHECK_INT(value, LocalMs(2024, 2, 29, 0, 0, 0));
}

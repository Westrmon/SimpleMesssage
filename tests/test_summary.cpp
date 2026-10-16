// SimpleMessage — include/sm/summary.h 与 host_context.h::allocate_string 的单元测试
#include <cstring>
#include <string>
#include <vector>

#include "sm/host_context.h"
#include "sm/summary.h"

#include "fake_host_context.h"
#include "test_framework.h"

namespace {

std::string summary_of(const char* src,
                       std::size_t max_codepoints = sm::limits::kSummaryCodepoints,
                       std::size_t dst_capacity = 256) {
  std::vector<char> buffer(dst_capacity, '\0');
  const std::size_t written =
      sm::make_summary(src, max_codepoints, buffer.data(), buffer.size());
  return std::string(buffer.data(), written);
}

}  // namespace

SM_TEST(utf8_sequence_length, classifies_every_lead_byte_range) {
  SM_CHECK_INT(sm::utf8_sequence_length(0x00), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0x41), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0x7F), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0xC2), 2);
  SM_CHECK_INT(sm::utf8_sequence_length(0xDF), 2);
  SM_CHECK_INT(sm::utf8_sequence_length(0xE4), 3);
  SM_CHECK_INT(sm::utf8_sequence_length(0xEF), 3);
  SM_CHECK_INT(sm::utf8_sequence_length(0xF0), 4);
  SM_CHECK_INT(sm::utf8_sequence_length(0xF4), 4);
}

SM_TEST(utf8_sequence_length, treats_invalid_leads_as_one_byte) {
  SM_CHECK_INT(sm::utf8_sequence_length(0x80), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0xBF), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0xF8), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0xFE), 1);
  SM_CHECK_INT(sm::utf8_sequence_length(0xFF), 1);
}

SM_TEST(utf8_codepoint_count, counts_codepoints_not_bytes) {
  SM_CHECK_INT(sm::utf8_codepoint_count(""), 0);
  SM_CHECK_INT(sm::utf8_codepoint_count("abc"), 3);
  SM_CHECK_INT(sm::utf8_codepoint_count("中文测试"), 4);
  SM_CHECK_INT(sm::utf8_codepoint_count("a中"), 2);
  SM_CHECK_INT(sm::utf8_codepoint_count(nullptr), 0);
  SM_CHECK_INT(sm::utf8_codepoint_count("\xF0\x9F\x98\x80"), 1);
}

SM_TEST(utf8_codepoint_count, survives_a_truncated_sequence) {
  SM_CHECK_INT(sm::utf8_codepoint_count("\xE4\xB8"), 1);
  SM_CHECK_INT(sm::utf8_codepoint_count("\xE4\xB8\xAD\xE6"), 2);
}

SM_TEST(utf8_next, never_steps_past_the_terminator) {
  SM_CHECK_INT(sm::utf8_next("abc", 0), 1);
  SM_CHECK_INT(sm::utf8_next("abc", 2), 3);
  SM_CHECK_INT(sm::utf8_next("ab", 1), 2);
  SM_CHECK_INT(sm::utf8_next("", 0), 0);
  SM_CHECK_INT(sm::utf8_next(nullptr, 0), 0);
  SM_CHECK_INT(sm::utf8_next("\xE4\xB8", 0), 2);
  SM_CHECK_INT(sm::utf8_next("\xE4\xB8\xAD", 0), 3);
}

SM_TEST(make_summary, folds_whitespace_runs_into_one_space) {
  SM_CHECK_TEXT(summary_of("hello"), "hello");
  SM_CHECK_TEXT(summary_of("hello world"), "hello world");
  SM_CHECK_TEXT(summary_of("  hello   world  "), "hello world");
  SM_CHECK_TEXT(summary_of("a\t\n\r\f\vb"), "a b");
  SM_CHECK_TEXT(summary_of("   "), "");
  SM_CHECK_TEXT(summary_of(""), "");
  SM_CHECK_TEXT(summary_of(nullptr), "");
}

SM_TEST(make_summary, folded_space_does_not_spend_the_quota) {
  SM_CHECK_TEXT(summary_of("a b c d", 3), "a b c\xE2\x80\xA6");
  SM_CHECK_TEXT(summary_of("a b c d", 4), "a b c d");
  SM_CHECK_INT(summary_of("abcd", 3).size(), 6);
  SM_CHECK_INT(summary_of("a b c d", 3).size(), 8);
}

SM_TEST(make_summary, keeps_non_ascii_space_as_a_character) {
  SM_CHECK_TEXT(summary_of("\xE3\x80\x80" "a", 20), "\xE3\x80\x80" "a");
}

SM_TEST(make_summary, adds_ellipsis_only_when_something_is_left_out) {
  SM_CHECK_TEXT(summary_of("abc", 3), "abc");
  SM_CHECK_TEXT(summary_of("abcd", 3), "abc\xE2\x80\xA6");
  SM_CHECK_TEXT(summary_of("ab", 3), "ab");
  SM_CHECK_TEXT(summary_of("abc   ", 3), "abc");
  SM_CHECK_TEXT(summary_of("abc   def", 3), "abc\xE2\x80\xA6");
}

SM_TEST(make_summary, never_cuts_a_utf8_sequence) {
  SM_CHECK_TEXT(summary_of("中文测试", 2), "中文\xE2\x80\xA6");
  SM_CHECK_TEXT(summary_of("中文测试", 4), "中文测试");
  SM_CHECK_TEXT(summary_of("中文测试", 3), "中文测\xE2\x80\xA6");

  SM_CHECK_TEXT(summary_of("中文", 1), "中\xE2\x80\xA6");
}

SM_TEST(make_summary, respects_the_destination_capacity) {
  char buffer[5];
  const std::size_t written = sm::make_summary("abcdef", 20, buffer, sizeof(buffer));
  SM_CHECK_INT(written, 4);
  SM_CHECK_TEXT(std::string(buffer, written), "abcd");
  SM_CHECK_INT(std::strlen(buffer), 4);
}

SM_TEST(make_summary, handles_degenerate_arguments) {
  char buffer[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};

  SM_CHECK_INT(sm::make_summary("abc", 20, nullptr, 8), 0);
  SM_CHECK_INT(sm::make_summary("abc", 20, buffer, 0), 0);
  SM_CHECK_TEXT(std::string(buffer, sizeof(buffer)), "xxxxxxxx");

  SM_CHECK_INT(sm::make_summary("abc", 0, buffer, sizeof(buffer)), 0);
  SM_CHECK_TEXT(std::string(buffer, std::strlen(buffer)), "");
  SM_CHECK_INT(sm::make_summary(nullptr, 20, buffer, sizeof(buffer)), 0);
  SM_CHECK_TEXT(std::string(buffer, std::strlen(buffer)), "");
}

SM_TEST(make_summary, produces_a_string_that_counts_back_correctly) {
  const std::string truncated = summary_of("这是一段很长的中文内容用来触发截断", 5);
  SM_CHECK_TEXT(truncated, "这是一段很\xE2\x80\xA6");
  SM_CHECK_INT(sm::utf8_codepoint_count(truncated.c_str()), 6);

  const char* whole = "这是一段中文";
  SM_CHECK_INT(sm::utf8_codepoint_count(summary_of(whole).c_str()),
               sm::utf8_codepoint_count(whole));
}

SM_TEST(allocate_string, copies_within_the_budget) {
  FakeHostContext host;
  const char* original = "hello";
  const char* copy = host.allocate_string(original, 100);
  SM_CHECK(copy != nullptr);
  SM_CHECK_TEXT(std::string(copy), "hello");
  SM_CHECK(copy != original);
}

SM_TEST(allocate_string, returns_null_for_null_and_for_an_exhausted_arena) {
  FakeHostContext host;
  SM_CHECK(host.allocate_string(nullptr, 100) == nullptr);

  FakeHostContext tiny(8);
  SM_CHECK(tiny.allocate_string("0123456789", 1000) == nullptr);
}

SM_TEST(allocate_string, truncates_by_bytes_and_can_split_a_utf8_sequence) {
  FakeHostContext host;
  const char* text = "中文测试";

  const char* copy = host.allocate_string(text, 4);
  SM_CHECK(copy != nullptr);
  SM_CHECK_INT(std::strlen(copy), 4);
  SM_CHECK(std::memcmp(copy, text, 4) == 0);
  SM_CHECK(copy[3] == text[3]);
  SM_CHECK_INT(sm::utf8_sequence_length(static_cast<unsigned char>(copy[3])), 3);
  SM_CHECK_INT(sm::utf8_next(copy, 3), 4);
  SM_CHECK_INT(sm::utf8_codepoint_count(copy), 2);

  SM_CHECK_TEXT(summary_of(text, 2, 64), "中文\xE2\x80\xA6");
}

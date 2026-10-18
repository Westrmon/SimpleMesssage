// SimpleMessage — 占位符展开与路径清洗单元测试
#include "test_framework.h"

#include <map>
#include <string>
#include <vector>

#include "host/placeholders.h"

namespace {

using sm::host::ExpandPlaceholders;
using sm::host::FindPlaceholderNames;
using sm::host::IsKnownNotificationPlaceholder;
using sm::host::PlaceholderMode;
using sm::host::PlaceholderResolver;
using sm::host::SanitizePathValue;

PlaceholderResolver Fixed(std::map<std::string, std::string> values) {
  return [values](std::string_view name, std::string* out) {
    const auto it = values.find(std::string(name));
    if (it == values.end()) return false;
    *out = it->second;
    return true;
  };
}

}  // namespace

SM_TEST(placeholders, expands_known_names) {
  const auto resolver = Fixed({{"year", "2026"}, {"month", "10"}, {"title", "你好"}});
  SM_CHECK_TEXT(ExpandPlaceholders("{year}-{month}/日志-{title}.txt",
                                   PlaceholderMode::Text, resolver),
                "2026-10/日志-你好.txt");
}

SM_TEST(placeholders, double_braces_are_literal) {
  const auto resolver = Fixed({{"year", "2026"}});
  SM_CHECK_TEXT(ExpandPlaceholders("{{{year}}}", PlaceholderMode::Text, resolver),
                "{2026}");
  SM_CHECK_TEXT(ExpandPlaceholders("a{{b}}c", PlaceholderMode::Text, resolver),
                "a{b}c");
}

SM_TEST(placeholders, unknown_names_are_kept_and_reported) {
  const auto resolver = Fixed({{"known", "ok"}});
  std::vector<std::string> unknown;
  const std::string out = ExpandPlaceholders(
      "{known}-{nope}-{nope}", PlaceholderMode::Text, resolver, &unknown);
  SM_CHECK_TEXT(out, "ok-{nope}-{nope}");
  SM_CHECK_INT(unknown.size(), 1);
  SM_CHECK_TEXT(unknown[0], "nope");
}

SM_TEST(placeholders, path_mode_sanitizes_values_only) {
  const auto resolver = Fixed({{"title", "a/b:c*?"}});
  SM_CHECK_TEXT(
      ExpandPlaceholders("../{title}.txt", PlaceholderMode::Path, resolver),
      "../a_b_c__.txt");
  SM_CHECK_TEXT(
      ExpandPlaceholders("../{title}.txt", PlaceholderMode::Text, resolver),
      "../a/b:c*?.txt");
}

SM_TEST(placeholders, finds_names_ordered_and_unique) {
  const std::vector<std::string> names =
      FindPlaceholderNames("{{skip}}{b}/{a}/{b}/{not valid}/{9}");
  const std::vector<std::string> expected = {"b", "a", "9"};
  SM_CHECK_INT(names.size(), 3);
  for (std::size_t i = 0; i < expected.size() && i < names.size(); ++i) {
    SM_CHECK_TEXT(names[i], expected[i]);
  }
}

SM_TEST(placeholders, known_names) {
  SM_CHECK(IsKnownNotificationPlaceholder("date"));
  SM_CHECK(IsKnownNotificationPlaceholder("ruleid"));
  SM_CHECK(IsKnownNotificationPlaceholder("notifier"));
  SM_CHECK(!IsKnownNotificationPlaceholder("unknown"));
  SM_CHECK(!IsKnownNotificationPlaceholder(""));
}

SM_TEST(placeholders, sanitize_replaces_illegal_and_trims) {
  SM_CHECK_TEXT(SanitizePathValue("a<b>c:d\"e/f\\g|h?i*j"),
                "a_b_c_d_e_f_g_h_i_j");
  SM_CHECK_TEXT(SanitizePathValue("name...  "), "name");
  SM_CHECK_TEXT(SanitizePathValue("."), "_");
  SM_CHECK_TEXT(SanitizePathValue(".."), "_");
  SM_CHECK_TEXT(SanitizePathValue(""), "_");
  SM_CHECK_TEXT(SanitizePathValue("CON"), "_CON");
  SM_CHECK_TEXT(SanitizePathValue("nul.txt"), "_nul.txt");
  SM_CHECK_TEXT(SanitizePathValue("console"), "console");
}

SM_TEST(placeholders, sanitize_truncates_on_utf8_boundary) {
  std::string text;
  for (int i = 0; i < 50; ++i) text += "中";
  std::string expected;
  for (int i = 0; i < 40; ++i) expected += "中";
  const std::string out = SanitizePathValue(text);
  SM_CHECK_INT(out.size(), 120);
  SM_CHECK_TEXT(out, expected);
}

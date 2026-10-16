// SimpleMessage — include/sm/version_range.h 的单元测试
#include <cstring>

#include "sm/version_range.h"

#include "test_framework.h"

namespace {

bool parses_as(const char* text, int major, int minor, int patch) {
  sm::Version v;
  if (!sm::parse_version(text, &v)) return false;
  return v.major == major && v.minor == minor && v.patch == patch;
}

}  // namespace

SM_TEST(parse_version, accepts_one_two_and_three_parts) {
  SM_CHECK(parses_as("1", 1, 0, 0));
  SM_CHECK(parses_as("1.2", 1, 2, 0));
  SM_CHECK(parses_as("1.2.3", 1, 2, 3));
  SM_CHECK(parses_as("0.1.0", 0, 1, 0));
  SM_CHECK(parses_as("2026.10.15", 2026, 10, 15));
}

SM_TEST(parse_version, tolerates_surrounding_space) {
  SM_CHECK(parses_as("1.2.3 ", 1, 2, 3));
  SM_CHECK(parses_as(" 1.2.3", 1, 2, 3));
  SM_CHECK(parses_as("\t1.2.3\n", 1, 2, 3));
  SM_CHECK(parses_as("01.002.0003", 1, 2, 3));
  sm::Version v;
  SM_CHECK(!sm::parse_version("1. 2 .3", &v));
}

SM_TEST(parse_version, rejects_malformed) {
  sm::Version v;
  SM_CHECK(!sm::parse_version("1.2.3.4", &v));
  SM_CHECK(!sm::parse_version("v1.2", &v));
  SM_CHECK(!sm::parse_version("1.2.3-beta", &v));
  SM_CHECK(!sm::parse_version("1.2.3+sha", &v));
  SM_CHECK(!sm::parse_version("", &v));
  SM_CHECK(!sm::parse_version("   ", &v));
  SM_CHECK(!sm::parse_version("1.", &v));
  SM_CHECK(!sm::parse_version(".1", &v));
  SM_CHECK(!sm::parse_version("1..2", &v));
  SM_CHECK(!sm::parse_version("-1", &v));
  SM_CHECK(!sm::parse_version("1.2.x", &v));
  SM_CHECK(!sm::parse_version("1 2", &v));
  SM_CHECK(!sm::parse_version(nullptr, &v));
  SM_CHECK(!sm::parse_version("1.2.3", nullptr));

  sm::Version untouched{7, 7, 7};
  SM_CHECK(!sm::parse_version("nonsense", &untouched));
  SM_CHECK_INT(untouched.major, 7);
  SM_CHECK_INT(untouched.minor, 7);
  SM_CHECK_INT(untouched.patch, 7);
}

SM_TEST(compare_version, orders_by_major_then_minor_then_patch) {
  const sm::Version a{1, 2, 3};
  const sm::Version same{1, 2, 3};
  const sm::Version higher_major{2, 0, 0};
  const sm::Version higher_minor{1, 3, 0};
  const sm::Version higher_patch{1, 2, 4};
  const sm::Version lower{1, 2, 2};

  SM_CHECK_INT(sm::compare_version(a, same), 0);
  SM_CHECK_INT(sm::compare_version(a, higher_major), -1);
  SM_CHECK_INT(sm::compare_version(a, higher_minor), -1);
  SM_CHECK_INT(sm::compare_version(a, higher_patch), -1);
  SM_CHECK_INT(sm::compare_version(a, lower), 1);
  SM_CHECK_INT(sm::compare_version(higher_major, a), 1);
}

SM_TEST(is_valid_range, accepts_the_documented_subset) {
  SM_CHECK(sm::is_valid_range(""));
  SM_CHECK(sm::is_valid_range("   "));
  SM_CHECK(sm::is_valid_range("1.0.0"));
  SM_CHECK(sm::is_valid_range("=1.0.0"));
  SM_CHECK(sm::is_valid_range(">=1.0.0"));
  SM_CHECK(sm::is_valid_range(">1.0.0"));
  SM_CHECK(sm::is_valid_range("<=1.0.0"));
  SM_CHECK(sm::is_valid_range("<1.0.0"));
  SM_CHECK(sm::is_valid_range(">=1.0.0,<2.0.0"));
  SM_CHECK(sm::is_valid_range(">=1.0.0 | <0.5.0"));
  SM_CHECK(sm::is_valid_range(" >= 1.0.0 , < 2.0.0 "));
}

SM_TEST(is_valid_range, rejects_what_the_sdk_says_it_does_not_support) {
  SM_CHECK(!sm::is_valid_range(nullptr));
  SM_CHECK(!sm::is_valid_range(">="));
  SM_CHECK(!sm::is_valid_range(","));
  SM_CHECK(!sm::is_valid_range("|"));
  SM_CHECK(!sm::is_valid_range(",1.0.0"));
  SM_CHECK(!sm::is_valid_range("|1.0.0"));
  SM_CHECK(!sm::is_valid_range(">=1.0.0 || <2.0.0"));
  SM_CHECK(!sm::is_valid_range("^1.0.0"));
  SM_CHECK(!sm::is_valid_range("~1.0.0"));
  SM_CHECK(!sm::is_valid_range("1.*"));
  SM_CHECK(!sm::is_valid_range("1.0 - 2.0"));
  SM_CHECK(!sm::is_valid_range("1.0.0 2.0.0"));
  SM_CHECK(!sm::is_valid_range(">=1.0.0-beta"));
}

SM_TEST(is_valid_range, tolerates_a_trailing_separator) {
  SM_CHECK(sm::is_valid_range(">=1.0.0,"));
  SM_CHECK(sm::is_valid_range(">=1.0.0 |"));
  SM_CHECK(sm::version_satisfies("9.9.9", ">=1.0.0 |"));
}

SM_TEST(version_satisfies, and_groups_are_closed_intervals) {
  SM_CHECK(sm::version_satisfies("1.0.0", ">=1.0.0,<2.0.0"));
  SM_CHECK(sm::version_satisfies("1.5.0", ">=1.0.0,<2.0.0"));
  SM_CHECK(sm::version_satisfies("1.9.9", ">=1.0.0,<2.0.0"));
  SM_CHECK(!sm::version_satisfies("2.0.0", ">=1.0.0,<2.0.0"));
  SM_CHECK(!sm::version_satisfies("0.9.9", ">=1.0.0,<2.0.0"));

  SM_CHECK(sm::version_satisfies("2.0.0", "<=2.0.0"));
  SM_CHECK(!sm::version_satisfies("2.0.1", "<=2.0.0"));
}

SM_TEST(version_satisfies, or_groups_are_tried_in_order) {
  const char* range = ">=1.0.0 | <0.5.0";
  SM_CHECK(sm::version_satisfies("1.0.0", range));
  SM_CHECK(sm::version_satisfies("2.0.0", range));
  SM_CHECK(sm::version_satisfies("0.4.9", range));
  SM_CHECK(!sm::version_satisfies("0.7.0", range));
  SM_CHECK(!sm::version_satisfies("0.9.0", range));
}

SM_TEST(version_satisfies, bare_version_means_equality) {
  SM_CHECK(sm::version_satisfies("1.2.3", "1.2.3"));
  SM_CHECK(sm::version_satisfies("1.2.3", "=1.2.3"));
  SM_CHECK(!sm::version_satisfies("1.2.3", "1.2"));
  SM_CHECK(sm::version_satisfies("1.2.0", "1.2"));
}

SM_TEST(version_satisfies, empty_range_means_unlimited) {
  SM_CHECK(sm::version_satisfies("0.0.1", ""));
  SM_CHECK(sm::version_satisfies("99.99.99", ""));
  SM_CHECK(sm::version_satisfies("0.0.1", "   "));
}

SM_TEST(version_satisfies, reports_three_distinct_failure_reasons) {
  const char* bad_version = nullptr;
  const char* bad_range = nullptr;
  const char* unsatisfied = nullptr;

  SM_CHECK(!sm::version_satisfies("1.2.3-beta", ">=1.0.0", &bad_version));
  SM_CHECK(!sm::version_satisfies("1.0.0", "^1.0.0", &bad_range));
  SM_CHECK(!sm::version_satisfies("0.1.0", ">=1.0.0", &unsatisfied));

  SM_CHECK(bad_version != nullptr);
  SM_CHECK(bad_range != nullptr);
  SM_CHECK(unsatisfied != nullptr);
  SM_CHECK(std::strcmp(bad_version, bad_range) != 0);
  SM_CHECK(std::strcmp(bad_range, unsatisfied) != 0);
  SM_CHECK(std::strcmp(bad_version, unsatisfied) != 0);

  const char* stale = "stale";
  SM_CHECK(sm::version_satisfies("1.5.0", ">=1.0.0", &stale));
  SM_CHECK(stale == nullptr);

  SM_CHECK(!sm::version_satisfies("0.1.0", ">=1.0.0"));
}

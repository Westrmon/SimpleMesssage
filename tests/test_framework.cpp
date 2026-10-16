#include "test_framework.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace smtest {
namespace {

struct Case {
  const char* suite;
  const char* name;
  TestFn fn;
};

std::vector<Case>& Registry() {
  static std::vector<Case> registry;
  return registry;
}

std::size_t g_case_checks = 0;
std::size_t g_case_failures = 0;

void Report(const char* file, int line, const std::string& detail) {
  std::printf("         %s:%d\n", file, line);
  std::printf("         %s\n", detail.c_str());
}

}  // namespace

bool AddCase(const char* suite, const char* name, TestFn fn) {
  Registry().push_back(Case{suite, name, fn});
  return true;
}

void Check(bool ok, const char* expr, const char* file, int line) {
  ++g_case_checks;
  if (ok) return;
  ++g_case_failures;
  Report(file, line, std::string("expected: ") + expr);
}

void CheckEqText(std::string actual, std::string expected, const char* file, int line) {
  ++g_case_checks;
  if (actual == expected) return;
  ++g_case_failures;
  Report(file, line,
         "actual:   \"" + actual + "\"\n         expected: \"" + expected + "\"");
}

void CheckEqNumber(long long actual, long long expected, const char* file, int line) {
  ++g_case_checks;
  if (actual == expected) return;
  ++g_case_failures;
  Report(file, line,
         "actual:   " + std::to_string(actual) + "\n         expected: " +
             std::to_string(expected));
}

int RunAll() {
  std::size_t failed_cases = 0;
  std::size_t total_checks = 0;
  std::size_t total_failures = 0;
  const char* current_suite = nullptr;

  for (const Case& item : Registry()) {
    if (current_suite == nullptr || std::strcmp(current_suite, item.suite) != 0) {
      std::printf("\n[%s]\n", item.suite);
      current_suite = item.suite;
    }

    g_case_checks = 0;
    g_case_failures = 0;
    item.fn();
    total_checks += g_case_checks;
    total_failures += g_case_failures;

    if (g_case_failures == 0) {
      std::printf("  ok    %s\n", item.name);
    } else {
      ++failed_cases;
      std::printf("  FAIL  %s (%zu of %zu checks)\n", item.name, g_case_failures,
                  g_case_checks);
    }
  }

  std::printf("\n%zu checks, %zu failed; %zu failed cases\n", total_checks,
              total_failures, failed_cases);
  return static_cast<int>(failed_cases);
}

}  // namespace smtest

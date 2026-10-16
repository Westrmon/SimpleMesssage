// SimpleMessage — 极简测试框架
#pragma once

#include <string>

namespace smtest {

using TestFn = void (*)();

bool AddCase(const char* suite, const char* name, TestFn fn);

void Check(bool ok, const char* expr, const char* file, int line);
void CheckEqText(std::string actual, std::string expected, const char* file, int line);
void CheckEqNumber(long long actual, long long expected, const char* file, int line);

int RunAll();

}  // namespace smtest

#define SM_TEST(suite, name)                                            \
  static void sm_test_##suite##_##name();                               \
  [[maybe_unused]] static const bool sm_reg_##suite##_##name =          \
      ::smtest::AddCase(#suite, #name, &sm_test_##suite##_##name);      \
  static void sm_test_##suite##_##name()

#define SM_CHECK(cond) ::smtest::Check((cond), #cond, __FILE__, __LINE__)

#define SM_CHECK_TEXT(actual, expected) \
  ::smtest::CheckEqText((actual), (expected), __FILE__, __LINE__)

#define SM_CHECK_INT(actual, expected)                                        \
  ::smtest::CheckEqNumber(static_cast<long long>(actual),                     \
                          static_cast<long long>(expected), __FILE__, __LINE__)

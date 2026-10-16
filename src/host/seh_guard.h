// SimpleMessage — 跨插件调用的崩溃屏障
#pragma once

#include <cstdint>

namespace sm {
namespace host {
namespace guard {

struct Outcome {
  bool          ok = true;
  std::uint32_t exception_code = 0;

  bool crashed() const { return !ok; }
};

Outcome Invoke(void (*fn)(void*), void* user);

const char* ExceptionText(std::uint32_t code);

void ReportCrash(const char* plugin_id, const char* method, const Outcome& outcome);

}  // namespace guard
}  // namespace host
}  // namespace sm

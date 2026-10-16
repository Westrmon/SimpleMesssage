// SimpleMessage — 崩溃屏障的实现
#include "host/seh_guard.h"

#include <cstdio>

#include "host/log.h"
#include "platform/platform.h"

namespace sm {
namespace host {
namespace guard {

namespace {

struct CodeText {
  std::uint32_t code;
  const char*   text;
};

constexpr CodeText kCodes[] = {
    {0x40010006u, "调试打印（插件的 OutputDebugString 被当作异常处理）"},
    {0x80000002u, "数据类型未对齐"},
    {0x80000003u, "命中断点"},
    {0xC0000005u, "访问违例（读写了一块不属于自己的内存）"},
    {0xC0000006u, "内存页调入失败（文件被截断或介质错误）"},
    {0xC000001Du, "非法指令"},
    {0xC000008Cu, "数组越界"},
    {0xC000008Du, "浮点非正规操作数"},
    {0xC000008Eu, "浮点除零"},
    {0xC0000090u, "浮点非法操作"},
    {0xC0000091u, "浮点上溢"},
    {0xC0000092u, "浮点栈检查失败"},
    {0xC0000093u, "浮点下溢"},
    {0xC0000094u, "整数除零"},
    {0xC0000095u, "整数上溢"},
    {0xC0000096u, "执行了特权指令"},
    {0xC00000FDu, "栈溢出（多半是无限递归）"},
    {0xC0000409u, "检测到栈缓冲区被破坏（/GS 检查失败）"},
    {0xE06D7363u, "C++ 异常越过了插件边界"},
};

}  // namespace

Outcome Invoke(void (*fn)(void*), void* user) {
  Outcome outcome;
  std::uint32_t code = 0;
  outcome.ok = platform::guarded_call(fn, user, &code);
  outcome.exception_code = code;
  return outcome;
}

const char* ExceptionText(std::uint32_t code) {
  for (const CodeText& entry : kCodes) {
    if (entry.code == code) return entry.text;
  }
  return nullptr;
}

void ReportCrash(const char* plugin_id, const char* method, const Outcome& outcome) {
  const char* const id = (plugin_id != nullptr && *plugin_id != '\0') ? plugin_id : "(未知插件)";
  const char* const where = (method != nullptr) ? method : "(未知方法)";
  const char* const text = ExceptionText(outcome.exception_code);

  char buffer[512];
  if (text != nullptr) {
    std::snprintf(buffer, sizeof(buffer),
                  "插件 %s 在 %s() 内崩溃：%s（异常码 0x%08X）。该插件将被禁用。",
                  id, where, text, static_cast<unsigned>(outcome.exception_code));
  } else {
    std::snprintf(buffer, sizeof(buffer),
                  "插件 %s 在 %s() 内崩溃：未知异常（异常码 0x%08X）。该插件将被禁用。",
                  id, where, static_cast<unsigned>(outcome.exception_code));
  }

  log::WriteCritical(sm::LogLevel::Error, id, buffer);
  log::ForceFlush();
}

}  // namespace guard
}  // namespace host
}  // namespace sm

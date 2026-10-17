// SimpleMessage — Win32 平台层：进程入口

#include "platform/entry.h"

#include <windows.h>

#include "platform/system.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const sm::platform::EntryFunction entry = sm::platform::EntrySlot();
  if (entry == nullptr) return 1;
  return entry(sm::platform::command_line_args());
}

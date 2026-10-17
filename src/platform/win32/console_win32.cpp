// SimpleMessage — Win32 平台层：控制台附加与写入

#include "platform/console.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <windows.h>

#include "platform/utf8.h"

namespace sm {
namespace platform {
namespace console {
namespace {

bool g_ready = false;

bool stream_writable(HANDLE handle) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
  const DWORD type = ::GetFileType(handle);
  if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) return true;
  DWORD mode = 0;
  return ::GetConsoleMode(handle, &mode) != 0;
}

void point_std_streams_at_console() {
  FILE* stream = nullptr;
  ::freopen_s(&stream, "CONOUT$", "w", stdout);
  ::freopen_s(&stream, "CONOUT$", "w", stderr);
}

void write_to_handle(HANDLE handle, const char* utf8_text) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE || utf8_text == nullptr) {
    return;
  }
  DWORD written = 0;
  DWORD mode = 0;
  if (::GetConsoleMode(handle, &mode)) {
    const std::wstring wide = utf8::widen(utf8_text);
    ::WriteConsoleW(handle, wide.c_str(), static_cast<DWORD>(wide.size()), &written,
                    nullptr);
    return;
  }
  const std::size_t length = std::strlen(utf8_text);
  ::WriteFile(handle, utf8_text, static_cast<DWORD>(length), &written, nullptr);
}

HANDLE output_handle(bool is_error) {
  const DWORD id = is_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
  HANDLE handle = ::GetStdHandle(id);
  if (!stream_writable(handle)) {
    EnsureOutput();
    handle = ::GetStdHandle(id);
  }
  return handle;
}

}  // namespace

bool EnsureOutput() {
  if (g_ready) return true;
  g_ready = true;

  if (stream_writable(::GetStdHandle(STD_OUTPUT_HANDLE))) {
    ::AttachConsole(ATTACH_PARENT_PROCESS);
    return true;
  }

  if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole()) {
    point_std_streams_at_console();
    return true;
  }
  return false;
}

bool available() { return g_ready; }

void Write(bool is_error, const char* utf8_text) {
  write_to_handle(output_handle(is_error), utf8_text);
}

}  // namespace console
}  // namespace platform
}  // namespace sm

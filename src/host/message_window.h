// SimpleMessage — 宿主自己的那扇隐藏窗口
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sm {
namespace host {

class MessageWindow {
 public:
  using MessageFn = bool (*)(void* user, HWND hwnd, UINT message, WPARAM wparam,
                             LPARAM lparam);

  MessageWindow() = default;
  ~MessageWindow();

  MessageWindow(const MessageWindow&) = delete;
  MessageWindow& operator=(const MessageWindow&) = delete;

  bool Create(MessageFn on_message, void* user);
  void Destroy();

  bool alive() const { return hwnd_ != nullptr; }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam);
  LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  HWND       hwnd_ = nullptr;
  MessageFn  on_message_ = nullptr;
  void*      user_ = nullptr;
  bool       destroying_ = false;
};

}  // namespace host
}  // namespace sm

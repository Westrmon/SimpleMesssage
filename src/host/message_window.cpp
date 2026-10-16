// SimpleMessage — 宿主隐藏窗口的实现
#include "host/message_window.h"

#include "host/log.h"
#include "platform/win32/win32_shared.h"

namespace sm {
namespace host {

bool MessageWindow::Create(MessageFn on_message, void* user) {
  if (hwnd_ != nullptr) return true;

  const HINSTANCE instance = ::GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &MessageWindow::WndProcThunk;
  wc.hInstance = instance;
  wc.lpszClassName = platform::win32::kWakeWindowClass;
  if (::RegisterClassExW(&wc) == 0 &&
      ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    sm::log::Writef(sm::LogLevel::Error, "host",
                    "注册宿主消息窗口类失败（Win32 错误 %lu）",
                    static_cast<unsigned long>(::GetLastError()));
    return false;
  }

  on_message_ = on_message;
  user_ = user;

  hwnd_ = ::CreateWindowExW(0, platform::win32::kWakeWindowClass, L"", WS_POPUP, 0, 0,
                            0, 0, nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) {
    sm::log::Writef(sm::LogLevel::Error, "host",
                    "创建宿主消息窗口失败（Win32 错误 %lu）",
                    static_cast<unsigned long>(::GetLastError()));
    on_message_ = nullptr;
    user_ = nullptr;
    return false;
  }
  return true;
}

MessageWindow::~MessageWindow() { Destroy(); }

void MessageWindow::Destroy() {
  if (hwnd_ == nullptr) return;
  destroying_ = true;
  ::DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  destroying_ = false;
  on_message_ = nullptr;
  user_ = nullptr;
}

LRESULT CALLBACK MessageWindow::WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                             LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const CREATESTRUCTW* const cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }
  auto* const self =
      reinterpret_cast<MessageWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return ::DefWindowProcW(hwnd, message, wparam, lparam);
  return self->WndProc(hwnd, message, wparam, lparam);
}

LRESULT MessageWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      return 0;
    case WM_DESTROY:
      if (!destroying_) {
        sm::log::Write(sm::LogLevel::Warn, "host",
                       "宿主消息窗口被外部销毁，之后将无法被唤起");
      }
      return 0;
    default:
      break;
  }

  if (on_message_ != nullptr &&
      on_message_(user_, hwnd, message, wparam, lparam)) {
    return 0;
  }
  return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace host
}  // namespace sm

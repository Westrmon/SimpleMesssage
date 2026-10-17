// SimpleMessage — Win32 平台层：宿主隐藏消息窗口的实现

#include "platform/message_window.h"

#include <windows.h>

#include "host/log.h"
#include "platform/win32/win32_shared.h"

namespace sm {
namespace platform {
namespace {

class Win32MessageWindow final : public IMessageWindow {
 public:
  Win32MessageWindow() = default;
  ~Win32MessageWindow() override { Destroy(); }

  bool Create(SystemEventFn on_event, void* user) override {
    if (hwnd_ != nullptr) return true;

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Win32MessageWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = win32::kWakeWindowClass;
    if (::RegisterClassExW(&wc) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      sm::log::Writef(sm::LogLevel::Error, "host",
                      "注册宿主消息窗口类失败（Win32 错误 %lu）",
                      static_cast<unsigned long>(::GetLastError()));
      return false;
    }

    on_event_ = on_event;
    user_ = user;

    hwnd_ = ::CreateWindowExW(0, win32::kWakeWindowClass, L"", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
      sm::log::Writef(sm::LogLevel::Error, "host",
                      "创建宿主消息窗口失败（Win32 错误 %lu）",
                      static_cast<unsigned long>(::GetLastError()));
      on_event_ = nullptr;
      user_ = nullptr;
      return false;
    }
    return true;
  }

  void Destroy() override {
    if (hwnd_ == nullptr) return;
    destroying_ = true;
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    destroying_ = false;
    on_event_ = nullptr;
    user_ = nullptr;
  }

  bool alive() const override { return hwnd_ != nullptr; }

 private:
  static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const CREATESTRUCTW* const cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
    auto* const self =
        reinterpret_cast<Win32MessageWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    return self->WndProc(hwnd, message, wparam, lparam);
  }

  LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
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

    SystemEvent event;
    if (message == win32::kWakeMessage) {
      event.kind = SystemEventKind::kWake;
    } else if (message == WM_QUERYENDSESSION) {
      event.kind = SystemEventKind::kSessionQueryEnd;
    } else if (message == WM_ENDSESSION) {
      event.kind = SystemEventKind::kSessionEnd;
      event.ending = wparam != 0;
    } else {
      return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }

    if (on_event_ != nullptr && on_event_(user_, event)) return 0;
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }

  HWND         hwnd_ = nullptr;
  SystemEventFn on_event_ = nullptr;
  void*        user_ = nullptr;
  bool         destroying_ = false;
};

}  // namespace

IMessageWindow* NewMessageWindow() { return new Win32MessageWindow(); }

}  // namespace platform
}  // namespace sm

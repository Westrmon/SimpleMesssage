// SimpleMessage — Win32 托盘图标与右键菜单的实现

#include "platform/win32/tray.h"

#include <cwchar>
#include <new>
#include <string>

#include <windows.h>
#include <shellapi.h>

#include "host/log.h"
#include "host/util.h"
#include "platform/win32/icon.h"

namespace sm {
namespace platform {
namespace win32 {
namespace {

constexpr wchar_t kTrayWindowClass[] = L"SimpleMessage.TrayWindow";
constexpr UINT    kTrayCallbackMessage = 0x8000u + 20u;

enum MenuCommand : UINT {
  kCmdOpenPanel = 1,
  kCmdPause = 2,
  kCmdOpenConfigDir = 3,
  kCmdOpenLogDir = 4,
  kCmdAbout = 5,
  kCmdExit = 6,
};

int TrayIconSize() { return ::GetSystemMetrics(SM_CXSMICON); }

class Win32Tray final : public ITray {
 public:
  Win32Tray() = default;
  ~Win32Tray() override { Destroy(); }
  Win32Tray(const Win32Tray&) = delete;
  Win32Tray& operator=(const Win32Tray&) = delete;

  bool Create(const TrayLabels& labels, ITrayHost* host) override {
    if (hwnd_ != nullptr) return true;
    labels_ = labels;
    host_ = host;

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Win32Tray::WndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (::RegisterClassExW(&wc) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      sm::log::Writef(sm::LogLevel::Warn, "tray", "注册托盘窗口类失败（Win32 错误 %lu）",
                      static_cast<unsigned long>(::GetLastError()));
      return false;
    }

    hwnd_ = ::CreateWindowExW(0, kTrayWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                             nullptr, instance, this);
    if (hwnd_ == nullptr) {
      sm::log::Writef(sm::LogLevel::Warn, "tray", "创建托盘窗口失败（Win32 错误 %lu）",
                      static_cast<unsigned long>(::GetLastError()));
      return false;
    }

    taskbar_created_message_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    return AddIcon();
  }

  void Destroy() override {
    if (hwnd_ == nullptr) return;
    if (added_) {
      ::Shell_NotifyIconW(NIM_DELETE, &nid_);
      added_ = false;
    }
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    host_ = nullptr;
  }

  void SetLevel(const sm::Level* level) override {
    HICON resolved = LoadLevelIcon(level, TrayIconSize());
    if (resolved == nullptr) return;
    if (resolved == icon_) return;
    icon_ = resolved;
    if (!added_) return;
    ApplyFields();
    ::Shell_NotifyIconW(NIM_MODIFY, &nid_);
  }

  void ShowBalloon(const char* title_utf8, const char* text_utf8) override {
    if (!added_) return;
    const std::wstring title = util::widen(title_utf8);
    const std::wstring text = util::widen(text_utf8);
    nid_.uFlags |= NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    nid_.uTimeout = 10000;
    ::wcsncpy_s(nid_.szInfoTitle, title.c_str(), _TRUNCATE);
    ::wcsncpy_s(nid_.szInfo, text.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags &= ~NIF_INFO;
  }

 private:
  bool AddIcon() {
    ::ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    if (icon_ == nullptr) icon_ = LoadLevelIcon(nullptr, TrayIconSize());
    ApplyFields();
    if (!::Shell_NotifyIconW(NIM_ADD, &nid_)) {
      sm::log::Writef(sm::LogLevel::Warn, "tray", "托盘图标添加失败（Win32 错误 %lu）",
                      static_cast<unsigned long>(::GetLastError()));
      return false;
    }
    added_ = true;
    sm::log::Write(sm::LogLevel::Info, "tray", "托盘图标已就绪");
    return true;
  }

  void ApplyFields() {
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kTrayCallbackMessage;
    nid_.hIcon = icon_;
    ::wcsncpy_s(nid_.szTip, util::widen(labels_.tooltip).c_str(), _TRUNCATE);
  }

  void ShowMenu() {
    ::SetForegroundWindow(hwnd_);

    const HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) return;

    AppendItem(menu, kCmdOpenPanel, labels_.open_panel, false);
    AppendItem(menu, kCmdPause, labels_.pause,
               host_ != nullptr && host_->pause_checked());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendItem(menu, kCmdOpenConfigDir, labels_.open_config_dir, false);
    AppendItem(menu, kCmdOpenLogDir, labels_.open_log_dir, false);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendItem(menu, kCmdAbout, labels_.about, false);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendItem(menu, kCmdExit, labels_.exit, false);

    POINT cursor{};
    ::GetCursorPos(&cursor);
    const UINT chosen = ::TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
        hwnd_, nullptr);
    ::DestroyMenu(menu);

    ::PostMessageW(hwnd_, WM_NULL, 0, 0);

    Dispatch(chosen);
  }

  static void AppendItem(HMENU menu, UINT command, const char* label, bool checked) {
    const std::wstring text = util::widen(label);
    UINT flags = MF_STRING;
    if (checked) flags |= MF_CHECKED;
    ::AppendMenuW(menu, flags, command, text.c_str());
  }

  void Dispatch(UINT command) {
    if (host_ == nullptr) return;
    switch (command) {
      case kCmdOpenPanel:
        host_->OnOpenPanel();
        break;
      case kCmdPause:
        host_->OnTogglePause();
        break;
      case kCmdOpenConfigDir:
        host_->OnOpenConfigDir();
        break;
      case kCmdOpenLogDir:
        host_->OnOpenLogDir();
        break;
      case kCmdAbout:
        host_->OnAbout();
        break;
      case kCmdExit:
        host_->OnExit();
        break;
      default:
        break;
    }
  }

  static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const CREATESTRUCTW* const cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
    auto* const self = reinterpret_cast<Win32Tray*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    return self->WndProc(hwnd, message, wparam, lparam);
  }

  LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)wparam;

    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
      added_ = false;
      AddIcon();
      return 0;
    }
    if (message == kTrayCallbackMessage) {
      switch (LOWORD(lparam)) {
        case WM_LBUTTONUP:
          if (host_ != nullptr) host_->OnOpenPanel();
          return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
          ShowMenu();
          return 0;
        default:
          break;
      }
      return 0;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }

  HWND            hwnd_ = nullptr;
  ITrayHost*      host_ = nullptr;
  TrayLabels      labels_{};
  NOTIFYICONDATAW nid_{};
  HICON           icon_ = nullptr;
  bool            added_ = false;
  UINT            taskbar_created_message_ = 0;
};

}  // namespace

ITray* create_tray() { return new (std::nothrow) Win32Tray(); }

}  // namespace win32
}  // namespace platform
}  // namespace sm

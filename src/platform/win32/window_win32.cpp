// SimpleMessage — 窗口层：GLFW 实现（win32）
#include "platform/window.h"

#include <new>

#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#include "host/log.h"
#include "platform/win32/icon.h"

namespace sm {
namespace platform {
namespace window {
namespace {

void ApplyAppIcon(GLFWwindow* handle) {
  const HWND hwnd = ::glfwGetWin32Window(handle);
  if (hwnd == nullptr) return;
  const HICON big_icon = win32::LoadAppIcon(::GetSystemMetrics(SM_CXICON));
  const HICON small_icon = win32::LoadAppIcon(::GetSystemMetrics(SM_CXSMICON));
  if (big_icon != nullptr) {
    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(big_icon));
  }
  if (small_icon != nullptr) {
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(small_icon));
  }
}

void ApplyExStyles(GLFWwindow* handle, const WindowTraits& traits) {
  const HWND hwnd = ::glfwGetWin32Window(handle);
  if (hwnd == nullptr) return;
  LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (traits.no_activate) style |= WS_EX_NOACTIVATE;
  if (traits.tool_window) {
    style |= WS_EX_TOOLWINDOW;
    style &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
  }
  ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
}

ClientApi g_client_api = ClientApi::kNone;

void OnGlfwError(int code, const char* description) {
  sm::log::Writef(sm::LogLevel::Warn, "window", "GLFW 错误 %d：%s", code,
                  description != nullptr ? description : "(无说明)");
}

void ApplyClientApiHints(ClientApi api) {
  if (api == ClientApi::kNone) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

class GlfwWindow final : public IWindow {
 public:
  ~GlfwWindow() override { Destroy(); }

  bool Create(const WindowOptions& options) override {
    if (handle_ != nullptr) return true;

    const auto apply_hints = [&](ClientApi api) {
      glfwDefaultWindowHints();
      ApplyClientApiHints(api);
      glfwWindowHint(GLFW_VISIBLE, options.visible ? GLFW_TRUE : GLFW_FALSE);
      glfwWindowHint(GLFW_RESIZABLE, options.traits.resizable ? GLFW_TRUE : GLFW_FALSE);
      glfwWindowHint(GLFW_DECORATED, options.traits.decorated ? GLFW_TRUE : GLFW_FALSE);
      glfwWindowHint(GLFW_FLOATING, options.traits.floating ? GLFW_TRUE : GLFW_FALSE);
      glfwWindowHint(GLFW_FOCUS_ON_SHOW,
                     options.traits.no_activate ? GLFW_FALSE : GLFW_TRUE);
    };

    apply_hints(g_client_api);
    handle_ = glfwCreateWindow(options.width, options.height, options.title.c_str(),
                               nullptr, nullptr);
    if (handle_ == nullptr && g_client_api != ClientApi::kNone) {
      sm::log::Write(sm::LogLevel::Warn, "window",
                     "建立 OpenGL 上下文失败，改用无上下文窗口");
      apply_hints(ClientApi::kNone);
      handle_ = glfwCreateWindow(options.width, options.height, options.title.c_str(),
                                 nullptr, nullptr);
    }
    if (handle_ == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "window", "创建窗口失败");
      return false;
    }

    client_api_ = (glfwGetWindowAttrib(handle_, GLFW_CLIENT_API) == GLFW_OPENGL_API)
                      ? ClientApi::kOpenGl
                      : ClientApi::kNone;

    if (options.min_width > 0 && options.min_height > 0) {
      glfwSetWindowSizeLimits(handle_, options.min_width, options.min_height,
                              GLFW_DONT_CARE, GLFW_DONT_CARE);
    }

    ApplyExStyles(handle_, options.traits);

    ApplyAppIcon(handle_);

    callbacks_ = options.callbacks;
    glfwSetWindowUserPointer(handle_, this);
    glfwSetWindowCloseCallback(handle_, &GlfwWindow::OnCloseRequest);
    glfwSetWindowContentScaleCallback(handle_, &GlfwWindow::OnContentScaleChanged);
    return true;
  }

  void Destroy() override {
    if (handle_ == nullptr) return;
    callbacks_ = WindowCallbacks{};
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
    client_api_ = ClientApi::kNone;
  }

  bool alive() const override { return handle_ != nullptr; }

  void Show(bool focus) override {
    if (handle_ == nullptr) return;
    glfwShowWindow(handle_);
    if (!focus) return;
    glfwFocusWindow(handle_);
  }

  void Hide() override {
    if (handle_ != nullptr) glfwHideWindow(handle_);
  }

  bool visible() const override {
    return handle_ != nullptr && glfwGetWindowAttrib(handle_, GLFW_VISIBLE) != 0;
  }

  void SetTitle(const std::string& utf8_title) override {
    if (handle_ != nullptr) glfwSetWindowTitle(handle_, utf8_title.c_str());
  }

  void ClientSize(int* out_w, int* out_h) const override {
    int w = 0;
    int h = 0;
    if (handle_ != nullptr) glfwGetWindowSize(handle_, &w, &h);
    if (out_w != nullptr) *out_w = w;
    if (out_h != nullptr) *out_h = h;
  }

  void GetRect(int* x, int* y, int* w, int* h) const override {
    int pos_x = 0;
    int pos_y = 0;
    if (handle_ != nullptr) glfwGetWindowPos(handle_, &pos_x, &pos_y);
    if (x != nullptr) *x = pos_x;
    if (y != nullptr) *y = pos_y;
    ClientSize(w, h);
  }

  void SetRect(int x, int y, int w, int h) override {
    if (handle_ == nullptr) return;

    if (w > 0 && h > 0) glfwSetWindowSize(handle_, w, h);
    if (x != kUnsetPosition && y != kUnsetPosition) glfwSetWindowPos(handle_, x, y);
  }

  float ContentScale() const override {
    if (handle_ == nullptr) return 1.0f;
    float x_scale = 1.0f;
    float y_scale = 1.0f;
    glfwGetWindowContentScale(handle_, &x_scale, &y_scale);
    return x_scale > 0.0f ? x_scale : 1.0f;
  }

  void* native_handle() const override { return handle_; }

  ClientApi client_api() const override {
    return handle_ != nullptr ? client_api_ : ClientApi::kNone;
  }

 private:
  static void OnCloseRequest(GLFWwindow* handle) {
    // GLFW 的 Win32 事件泵一见 WM_QUIT 就给每个窗口打"应关闭"标记（win32_window.c
    // 里自己标了 HACK），所以这里要清掉它，把它当成单个窗口的关闭请求。
    auto* const self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(handle));
    glfwSetWindowShouldClose(handle, GLFW_FALSE);
    if (self != nullptr && self->callbacks_.on_close_request != nullptr) {
      self->callbacks_.on_close_request(self->callbacks_.user);
    }
  }

  static void OnContentScaleChanged(GLFWwindow* handle, float x_scale,
                                    float ) {
    auto* const self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(handle));
    if (self == nullptr || self->callbacks_.on_content_scale == nullptr) return;
    if (x_scale <= 0.0f) return;  // 拿到 0 会让字号变成 0，界面整片空白
    self->callbacks_.on_content_scale(self->callbacks_.user, x_scale);
  }

  GLFWwindow*    handle_ = nullptr;
  ClientApi      client_api_ = ClientApi::kNone;
  WindowCallbacks callbacks_{};
};

}  // namespace

IWindow* NewWindow() { return new (std::nothrow) GlfwWindow(); }

bool Init(ClientApi api) {
  g_client_api = api;
  glfwSetErrorCallback(&OnGlfwError);
  if (glfwInit() == GLFW_TRUE) return true;
  sm::log::Write(sm::LogLevel::Error, "window", "初始化窗口系统失败（GLFW）");
  return false;
}

void Shutdown() { glfwTerminate(); }

void Pump() { glfwPollEvents(); }

void Wait(int timeout_ms) {
  if (timeout_ms <= 0) {
    glfwPollEvents();
    return;
  }
  glfwWaitEventsTimeout(static_cast<double>(timeout_ms) / 1000.0);
}

WorkArea PrimaryWorkArea() {
  RECT rect{};
  if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0) != 0 &&
      rect.right > rect.left && rect.bottom > rect.top) {
    return WorkArea{rect.left, rect.top, rect.right - rect.left,
                    rect.bottom - rect.top};
  }

  sm::log::Write(sm::LogLevel::Warn, "window",
                 "取主显示器工作区失败，改用整块屏幕");
  return WorkArea{0, 0, ::GetSystemMetrics(SM_CXSCREEN),
                  ::GetSystemMetrics(SM_CYSCREEN)};
}

}  // namespace window
}  // namespace platform
}  // namespace sm

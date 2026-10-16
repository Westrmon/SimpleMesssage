// SimpleMessage — 窗口抽象：一扇窗口，以及它背后的窗口系统
#pragma once

#include <string>

namespace sm {
namespace platform {
namespace window {

enum class ClientApi {
  kNone,
  kOpenGl,
};

struct WindowCallbacks {
  void (*on_close_request)(void* user) = nullptr;
  void (*on_content_scale)(void* user, float scale) = nullptr;
  void* user = nullptr;
};

struct WindowTraits {
  bool resizable = true;
  bool decorated = true;
  bool floating = false;
  bool no_activate = false;
  bool tool_window = false;
};

struct WindowOptions {
  std::string title;
  int width = 0;
  int height = 0;
  int min_width = 0;
  int min_height = 0;
  WindowTraits traits{};
  bool visible = false;
  WindowCallbacks callbacks{};
};

constexpr int kUnsetPosition = -1;

class IWindow {
 public:
  virtual ~IWindow() = default;

  virtual bool Create(const WindowOptions& options) = 0;
  virtual void Destroy() = 0;
  virtual bool alive() const = 0;

  virtual void Show(bool focus = true) = 0;
  virtual void Hide() = 0;
  virtual bool visible() const = 0;
  virtual void SetTitle(const std::string& utf8_title) = 0;

  virtual void ClientSize(int* out_w, int* out_h) const = 0;
  virtual void GetRect(int* x, int* y, int* w, int* h) const = 0;
  virtual void SetRect(int x, int y, int w, int h) = 0;

  virtual float ContentScale() const = 0;

  virtual void* native_handle() const = 0;

  virtual ClientApi client_api() const = 0;
};

IWindow* NewWindow();

bool Init(ClientApi api);
void Shutdown();
void Pump();
void Wait(int timeout_ms);

struct WorkArea {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

WorkArea PrimaryWorkArea();

}  // namespace window
}  // namespace platform
}  // namespace sm

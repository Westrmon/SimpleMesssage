// SimpleMessage — UI 会话：imgui 上下文的引用计数与按需创建/销毁
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "host/plugin_host.h"
#include "host/render_backend.h"

namespace sm {
namespace platform {
namespace window {
class IWindow;
}  // namespace window
}  // namespace platform

namespace host {

class IUiRuntime {
 public:
  virtual ~IUiRuntime() = default;

  virtual bool Create(void** out_context, int font_size_px) = 0;
  virtual void Destroy() = 0;
  virtual void RebuildFonts(int font_size_px) = 0;
  virtual void SetContentScale(float scale) = 0;
  virtual void SetViewportSize(int width, int height) = 0;
  virtual bool alive() const = 0;

  // 取 / 创建本会话渲染上下文里的图标纹理。key 用于复用（图标来源），
  // 纹理由本 runtime 持有，Destroy() 时统一归还。
  virtual std::uint64_t AcquireTexture(const char* key, const void* rgba, int width,
                                       int height) = 0;

  virtual const std::string& font_file() const = 0;
};

struct ContentBounds {
  bool empty = true;
  int  x = 0;
  int  y = 0;
  int  w = 0;
  int  h = 0;
};

class IUiFrameRenderer {
 public:
  virtual ~IUiFrameRenderer() = default;
  virtual bool RenderFrame(void (*draw)(void* user), void* user) = 0;
  virtual ContentBounds content_bounds() const = 0;
};

enum class WindowSizing {
  kUserDriven,
  kContentDriven
};

IUiRuntime* CreateImGuiRuntime(platform::window::IWindow* window,
                               RendererKind renderer, WindowSizing sizing,
                               IUiFrameRenderer** out_frame);

using UiRuntimeFactory = IUiRuntime* (*)(void* user);

class UiSession final : public IUiSession {
 public:
  UiSession() = default;
  ~UiSession() override = default;
  UiSession(const UiSession&) = delete;
  UiSession& operator=(const UiSession&) = delete;

  void SetRuntime(IUiRuntime* runtime) { panel_.runtime = runtime; }
  void SetNotifyRuntimeSource(UiRuntimeFactory factory, void* user) {
    notify_factory_ = factory;
    notify_factory_user_ = user;
    if (factory == nullptr) notify_.runtime = nullptr;
  }
  IUiRuntime* notify_runtime() const { return notify_.runtime; }

  enum class Surface { kPanel, kNotify, kDetail };
  void SetDrawingSurface(Surface surface) { drawing_surface_ = surface; }
  // 详情窗口由 App 按插件各自建一扇，上下文也各自一份；画哪一扇由 App 每帧告知。
  void SetDetailContext(void* context) { detail_context_ = context; }

  void SetPanelOpen(bool open);
  bool panel_open() const { return panel_open_; }

  void* ImGuiContext() override;
  void RequestSurface(const std::string& plugin_id, std::int64_t duration_ms) override;
  void SetViewTitle(const std::string& plugin_id, const char* title) override;
  void CloseView(const std::string& plugin_id) override;

  enum class Change : std::int32_t {
    None = 0,
    Created,
    Destroyed,
  };
  struct Changes {
    Change panel = Change::None;
    Change notify = Change::None;
  };
  Changes Tick(std::int64_t now_ms);

  void ReleasePlugin(const std::string& plugin_id);

  void SetFontSize(int px);
  int  font_size() const { return font_size_px_; }
  void SetContentScale(Surface surface, float scale);

  // 详情视图有自己的窗口，不再借控制面板的窗口活着。
  bool panel_alive() const { return panel_open_; }
  bool notify_alive() const { return !surfaces_.empty(); }
  bool detail_alive() const { return !views_.empty(); }
  bool alive() const { return panel_alive() || notify_alive() || detail_alive(); }
  bool panel_runtime_alive() const { return panel_.runtime_alive; }
  bool notify_runtime_alive() const { return notify_.runtime_alive; }
  bool panel_create_failed() const { return panel_.create_failed; }

  struct ViewRef {
    std::string plugin_id;
    std::string title;
  };
  std::vector<ViewRef> Views() const;

 private:
  struct SurfaceRef {
    std::string  plugin_id;
    std::int64_t expires_ms = 0;
  };

  struct SurfaceState {
    IUiRuntime* runtime = nullptr;
    void*       context = nullptr;
    bool        runtime_alive = false;
    bool        create_failed = false;
  };

  Change TickSurface(SurfaceState& surface, bool want);

  static constexpr std::int64_t kForeverMs = 0x7FFFFFFFFFFFFFFFLL;
  struct View {
    std::string plugin_id;
    std::string title;
  };

  SurfaceRef* FindSurface(const std::string& plugin_id);
  View*       FindView(const std::string& plugin_id);
  void        ExpireSurfaces(std::int64_t now_ms);

  SurfaceState panel_;
  SurfaceState notify_;
  UiRuntimeFactory notify_factory_ = nullptr;
  void*            notify_factory_user_ = nullptr;
  bool             notify_factory_attempted_ = false;
  Surface          drawing_surface_ = Surface::kPanel;
  void*            detail_context_ = nullptr;
  bool        panel_open_ = false;
  std::vector<SurfaceRef> surfaces_;
  std::vector<View>       views_;
  int         font_size_px_ = 16;
};

}  // namespace host
}  // namespace sm

// SimpleMessage — UI 会话引用计数 + imgui 上下文/后端的具体实现
#include "host/ui_session.h"

#include <cmath>
#include <memory>
#include <string>

#include "GLFW/glfw3.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"

#include "platform/platform.h"
#include "platform/window.h"

#include "host/log.h"
#include "host/render_backend.h"
#include "host/ui_fonts.h"

namespace sm {
namespace host {
namespace {

float Clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

}  // namespace

class ImGuiRuntime final : public IUiRuntime, public IUiFrameRenderer {
 public:
  ImGuiRuntime(platform::window::IWindow* window, RendererKind renderer,
               WindowSizing sizing)
      : window_(window), renderer_(renderer), sizing_(sizing) {}

  bool Create(void** out_context, int font_size_px) override {
    if (context_ != nullptr) return true;
    if (window_ == nullptr || !window_->alive()) {
      sm::log::Write(sm::LogLevel::Error, "ui", "没有窗口，无法建立 UI 会话");
      return false;
    }

    // 必须自己 SetCurrentContext：已有当前上下文时 CreateContext 会把上一个恢复
    // 为当前，于是第二张表面的上下文刚建出来就不是当前了。
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::GetStyle().DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);

    font_file_ = ui_fonts::InstallInto(io.Fonts, static_cast<float>(font_size_px),
                                       nullptr);
    font_size_px_ = font_size_px;

    if (!ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(window_->native_handle()),
                                     true)) {
      sm::log::Write(sm::LogLevel::Error, "ui", "imgui 的 GLFW 后端初始化失败");
      ImGui::DestroyContext();
      return false;
    }
    window_backend_ready_ = true;

    const RendererKind kind = ResolveAgainstWindow(renderer_, window_->client_api());
    backend_.reset(CreateBackend(kind));
    if (backend_ == nullptr || !backend_->Init(window_)) {
      ImGui_ImplGlfw_Shutdown();
      window_backend_ready_ = false;
      backend_.reset();
      ImGui::DestroyContext();
      return false;
    }

    ImGui::GetStyle().FontSizeBase = static_cast<float>(font_size_px);
    ImGui::GetStyle().FontScaleDpi = window_->ContentScale();

    if (out_context != nullptr) *out_context = context_;
    sm::log::Writef(sm::LogLevel::Info, "ui", "UI 会话已创建（字号 %d，渲染后端 %s）",
                    font_size_px, backend_->name());
    return true;
  }

  void Destroy() override {
    if (context_ == nullptr && !window_backend_ready_ && backend_ == nullptr) return;
    // 先拨上下文再拆后端；拨错会拆掉另一张表面的后端。
    ImGui::SetCurrentContext(context_);
    if (backend_ != nullptr) {
      backend_->Shutdown();
      backend_.reset();
    }
    if (window_backend_ready_) {
      ImGui_ImplGlfw_Shutdown();
      window_backend_ready_ = false;
    }
    if (context_ != nullptr) {
      ImGui::DestroyContext(context_);
      context_ = nullptr;
    }
    sm::log::Write(sm::LogLevel::Info, "ui", "UI 会话已销毁");
  }

  void RebuildFonts(int font_size_px) override {
    if (context_ == nullptr) return;
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->ClearFonts();
    font_file_ = ui_fonts::InstallInto(io.Fonts, static_cast<float>(font_size_px),
                                       nullptr);
    font_size_px_ = font_size_px;

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = static_cast<float>(font_size_px);
    style._NextFrameFontSizeBase = static_cast<float>(font_size_px);
    sm::log::Writef(sm::LogLevel::Info, "ui", "字体图集已按新字号重建（%d）",
                    font_size_px);
  }

  void SetViewportSize(int width, int height) override {
    viewport_w_ = width;
    viewport_h_ = height;
  }

  void SetContentScale(float scale) override {
    if (context_ == nullptr || scale <= 0.0f) return;
    ImGui::GetStyle().FontScaleDpi = scale;
    sm::log::Writef(sm::LogLevel::Info, "ui", "界面缩放跟随显示器（%.2f）",
                    static_cast<double>(scale));
  }

  bool alive() const override {
    return context_ != nullptr && backend_ != nullptr && backend_->alive();
  }

  bool RenderFrame(void (*draw)(void* user), void* user) override {
    if (context_ == nullptr || backend_ == nullptr) return false;

    // 每帧开头都要重设：当前上下文是线程级单例。拨错的表现是"面板开着时浮层
    // 上的按钮点不动"，关掉面板又恢复正常。
    ImGui::SetCurrentContext(context_);

    ImGui_ImplGlfw_NewFrame();
    backend_->NewFrame();

    // 内容驱动的表面要把 DisplaySize 换成工作区而不是窗口，否则 ClipRect 会把内容
    // 锁在上一帧的尺寸上，底部按钮被裁掉且点不到。
    if (sizing_ == WindowSizing::kContentDriven && viewport_w_ > 0 && viewport_h_ > 0) {
      ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(viewport_w_),
                                          static_cast<float>(viewport_h_));
    }

    ImGui::NewFrame();

    if (draw != nullptr) draw(user);

    ImGui::Render();

    const float clear[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    if (!backend_->BeginFrame(clear)) return false;
    backend_->RenderDrawData();
    if (!backend_->Present()) return false;
    return true;
  }

  ContentBounds content_bounds() const override {
    // 只能量顶点包围盒，不能拿裁剪矩形当"内容范围"：窗口的第一条命令（背景底板）
    // 的 ClipRect 常是整块视口，量出来永远等于建窗时的默认尺寸。
    ContentBounds out;
    const ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr || data->CmdListsCount == 0) return out;

    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    bool any = false;
    for (int i = 0; i < data->CmdListsCount; ++i) {
      const ImDrawList* list = data->CmdLists[i];
      for (const ImDrawCmd& cmd : list->CmdBuffer) {
        if (cmd.ElemCount == 0) continue;
        const ImVec4& clip = cmd.ClipRect;
        if (clip.z <= clip.x || clip.w <= clip.y) continue;
        for (unsigned k = 0; k < cmd.ElemCount; ++k) {
          const ImDrawIdx index = list->IdxBuffer[cmd.IdxOffset + k];
          const ImVec2& pos = list->VtxBuffer[cmd.VtxOffset + index].pos;
          const float x = Clamp(pos.x, clip.x, clip.z);
          const float y = Clamp(pos.y, clip.y, clip.w);
          if (!any) {
            min_x = x; min_y = y; max_x = x; max_y = y;
            any = true;
            continue;
          }
          if (x < min_x) min_x = x;
          if (y < min_y) min_y = y;
          if (x > max_x) max_x = x;
          if (y > max_y) max_y = y;
        }
      }
    }
    if (!any) return out;

    const int left = static_cast<int>(static_cast<float>(std::floor(min_x)));
    const int top = static_cast<int>(static_cast<float>(std::floor(min_y)));
    const int right = static_cast<int>(static_cast<float>(std::ceil(max_x)));
    const int bottom = static_cast<int>(static_cast<float>(std::ceil(max_y)));

    out.empty = false;
    out.x = left;
    out.y = top;
    out.w = right - left;
    out.h = bottom - top;
    return out;
  }

  const std::string& font_file() const override { return font_file_; }

 private:
  platform::window::IWindow* window_ = nullptr;
  RendererKind               renderer_ = RendererKind::kAuto;
  WindowSizing               sizing_ = WindowSizing::kUserDriven;
  int                        viewport_w_ = 0;
  int                        viewport_h_ = 0;
  ImGuiContext*              context_ = nullptr;
  std::unique_ptr<IRenderBackend> backend_;
  bool                       window_backend_ready_ = false;
  std::string                font_file_;
  int                        font_size_px_ = 16;
};

IUiRuntime* CreateImGuiRuntime(platform::window::IWindow* window,
                               RendererKind renderer, WindowSizing sizing,
                               IUiFrameRenderer** out_frame) {
  auto* const runtime = new ImGuiRuntime(window, renderer, sizing);
  if (out_frame != nullptr) *out_frame = runtime;
  return runtime;
}

void UiSession::SetPanelOpen(bool open) {
  panel_open_ = open;
}

UiRefs UiSession::refs() const {
  UiRefs out;
  out.panel = panel_open_ ? 1 : 0;
  out.surfaces = static_cast<int>(surfaces_.size());
  out.views = static_cast<int>(views_.size());
  return out;
}

void* UiSession::ImGuiContext() {
  const SurfaceState& active =
      (drawing_surface_ == Surface::kNotify) ? notify_ : panel_;
  if (!active.runtime_alive) return nullptr;
  return active.context;
}

void UiSession::RequestSurface(const std::string& plugin_id, std::int64_t duration_ms) {
  if (plugin_id.empty()) return;
  const std::int64_t now = platform::monotonic_ms();

  SurfaceRef* existing = FindSurface(plugin_id);
  if (duration_ms <= 0) {
    if (existing == nullptr) {
      surfaces_.push_back(SurfaceRef{plugin_id, kForeverMs});
    } else {
      existing->expires_ms = kForeverMs;
    }
    return;
  }

  const std::int64_t expires = now + duration_ms;
  if (existing == nullptr) {
    surfaces_.push_back(SurfaceRef{plugin_id, expires});
  } else if (expires > existing->expires_ms) {
    existing->expires_ms = expires;
  }
}

void UiSession::SetViewTitle(const std::string& plugin_id, const char* title) {
  if (plugin_id.empty()) return;
  View* const existing = FindView(plugin_id);
  const char* const text = (title != nullptr && title[0] != '\0') ? title : "";
  if (existing != nullptr) {
    existing->title = text;
    return;
  }
  views_.push_back(View{plugin_id, text});
}

void UiSession::CloseView(const std::string& plugin_id) {
  for (std::size_t i = 0; i < views_.size(); ++i) {
    if (views_[i].plugin_id == plugin_id) {
      views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  for (std::size_t i = 0; i < surfaces_.size(); ++i) {
    if (surfaces_[i].plugin_id == plugin_id && surfaces_[i].expires_ms == kForeverMs) {
      surfaces_.erase(surfaces_.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
}

void UiSession::ReleasePlugin(const std::string& plugin_id) {
  for (std::size_t i = surfaces_.size(); i > 0; --i) {
    if (surfaces_[i - 1].plugin_id == plugin_id) {
      surfaces_.erase(surfaces_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }
  for (std::size_t i = views_.size(); i > 0; --i) {
    if (views_[i - 1].plugin_id == plugin_id) {
      views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }
}

void UiSession::SetFontSize(int px) {
  if (px == font_size_px_) return;
  font_size_px_ = px;
  if (panel_.runtime_alive && panel_.runtime != nullptr) {
    panel_.runtime->RebuildFonts(px);
  }
  if (notify_.runtime_alive && notify_.runtime != nullptr) {
    notify_.runtime->RebuildFonts(px);
  }
}

void UiSession::SetContentScale(Surface surface, float scale) {
  SurfaceState& target = (surface == Surface::kNotify) ? notify_ : panel_;
  if (target.runtime != nullptr) target.runtime->SetContentScale(scale);
}

UiSession::Changes UiSession::Tick(std::int64_t now_ms) {
  ExpireSurfaces(now_ms);

  if (notify_.runtime == nullptr && notify_factory_ != nullptr &&
      notify_factory_attempted_ == false && !surfaces_.empty()) {
    notify_factory_attempted_ = true;
    notify_.runtime = notify_factory_(notify_factory_user_);
  }

  Changes out;
  out.panel = TickSurface(panel_, panel_alive());
  out.notify = TickSurface(notify_, notify_alive());
  return out;
}

UiSession::Change UiSession::TickSurface(SurfaceState& surface, bool want) {
  if (surface.runtime_alive && surface.runtime != nullptr &&
      !surface.runtime->alive()) {
    surface.runtime->Destroy();
    surface.runtime_alive = false;
    surface.context = nullptr;
    surface.create_failed = true;
    return Change::Destroyed;
  }

  if (want && !surface.runtime_alive) {
    if (surface.create_failed) return Change::None;
    if (surface.runtime == nullptr) return Change::None;
    void* context = nullptr;
    if (surface.runtime->Create(&context, font_size_px_)) {
      surface.context = context;
      surface.runtime_alive = true;
      return Change::Created;
    }
    surface.create_failed = true;
    surface.context = nullptr;
    return Change::None;
  }

  if (!want) {
    surface.create_failed = false;
    if (surface.runtime_alive) {
      if (surface.runtime != nullptr) surface.runtime->Destroy();
      surface.runtime_alive = false;
      surface.context = nullptr;
      return Change::Destroyed;
    }
    return Change::None;
  }
  return Change::None;
}

void UiSession::ExpireSurfaces(std::int64_t now_ms) {
  for (std::size_t i = surfaces_.size(); i > 0; --i) {
    const SurfaceRef& ref = surfaces_[i - 1];
    if (ref.expires_ms != kForeverMs && now_ms >= ref.expires_ms) {
      surfaces_.erase(surfaces_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }
}

UiSession::SurfaceRef* UiSession::FindSurface(const std::string& plugin_id) {
  for (SurfaceRef& ref : surfaces_) {
    if (ref.plugin_id == plugin_id) return &ref;
  }
  return nullptr;
}

UiSession::View* UiSession::FindView(const std::string& plugin_id) {
  for (View& view : views_) {
    if (view.plugin_id == plugin_id) return &view;
  }
  return nullptr;
}

std::vector<UiSession::ViewRef> UiSession::Views() const {
  std::vector<ViewRef> out;
  out.reserve(views_.size());
  for (const View& view : views_) {
    out.push_back(ViewRef{view.plugin_id,
                          view.title.empty() ? view.plugin_id : view.title});
  }
  return out;
}

std::string UiSession::AliveReason() const {
  if (views_.empty()) return std::string();

  std::string reason;
  for (const View& view : views_) {
    if (!reason.empty()) reason += "、";
    reason += view.title.empty() ? view.plugin_id : view.title;
    reason += "（详情视图）";
  }
  return reason;
}

}  // namespace host
}  // namespace sm

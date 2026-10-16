// SimpleMessage — 渲染后端：把 imgui 的一帧真正画到窗口上
#pragma once

#include <string>

#include "platform/window.h"

namespace sm {
namespace host {

class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;

  virtual bool Init(platform::window::IWindow* window) = 0;
  virtual void Shutdown() = 0;

  virtual void NewFrame() = 0;
  virtual bool BeginFrame(const float clear_rgba[4]) = 0;
  virtual void RenderDrawData() = 0;
  virtual bool Present() = 0;

  virtual bool alive() const = 0;
  virtual const char* name() const = 0;
};

enum class RendererKind {
  kAuto,
  kDx11,
  kOpenGl,
};

const char* RendererName(RendererKind kind);

bool ParseRendererName(const std::string& text, RendererKind* out);

platform::window::ClientApi ClientApiFor(RendererKind kind);

RendererKind ProbeRenderer(RendererKind requested);
RendererKind ResolveAgainstWindow(RendererKind requested,
                                  platform::window::ClientApi actual);

IRenderBackend* CreateBackend(RendererKind kind);

IRenderBackend* CreateDx11Backend();
IRenderBackend* CreateOpenGlBackend();
bool Dx11HardwareAvailable();

}  // namespace host
}  // namespace sm

// SimpleMessage — 用哪个渲染后端：探测、覆盖与折算
#include "host/render_backend.h"

#include <cstring>

#include "host/log.h"

namespace sm {
namespace host {
namespace {

bool IsWord(const std::string& text, const char* word) {
  return ::_stricmp(text.c_str(), word) == 0;
}

}  // namespace

const char* RendererName(RendererKind kind) {
  switch (kind) {
    case RendererKind::kAuto:
      return "auto";
    case RendererKind::kDx11:
      return "DX11";
    case RendererKind::kOpenGl:
      return "OpenGL";
  }
  return "?";
}

bool ParseRendererName(const std::string& text, RendererKind* out) {
  if (out == nullptr) return false;
  if (IsWord(text, "auto")) {
    *out = RendererKind::kAuto;
    return true;
  }
  if (IsWord(text, "dx11")) {
    *out = RendererKind::kDx11;
    return true;
  }

  if (IsWord(text, "opengl") || IsWord(text, "gl")) {
    *out = RendererKind::kOpenGl;
    return true;
  }
  return false;
}

platform::window::ClientApi ClientApiFor(RendererKind kind) {
  if (kind == RendererKind::kOpenGl) return platform::window::ClientApi::kOpenGl;
  return platform::window::ClientApi::kNone;
}

RendererKind ProbeRenderer(RendererKind requested) {
  if (requested == RendererKind::kOpenGl) {
    sm::log::Write(sm::LogLevel::Info, "ui", "渲染器：OpenGL（--renderer 指定）");
    return requested;
  }

  if (Dx11HardwareAvailable()) {
    sm::log::Writef(sm::LogLevel::Info, "ui", "渲染器：DX11（%s，硬件设备）",
                    requested == RendererKind::kDx11 ? "--renderer 指定" : "自动探测");
    return RendererKind::kDx11;
  }

  if (requested == RendererKind::kDx11) {
    sm::log::Write(sm::LogLevel::Warn, "ui",
                   "渲染器：DX11（--renderer 指定）。没有硬件 D3D11 设备，将走软件光栅化");
    return RendererKind::kDx11;
  }

  sm::log::Write(sm::LogLevel::Warn, "ui",
                 "没有可用的硬件 D3D11 设备，渲染器改用 OpenGL");
  return RendererKind::kOpenGl;
}

RendererKind ResolveAgainstWindow(RendererKind requested,
                                  platform::window::ClientApi actual) {
  const bool got_context = actual == platform::window::ClientApi::kOpenGl;
  if (requested != RendererKind::kOpenGl || got_context) return requested;

  sm::log::Write(sm::LogLevel::Warn, "ui",
                 "窗口没有拿到 OpenGL 上下文（驱动不支持？），渲染器回退到 DX11");
  return RendererKind::kDx11;
}

IRenderBackend* CreateBackend(RendererKind kind) {
  if (kind == RendererKind::kOpenGl) return CreateOpenGlBackend();
  return CreateDx11Backend();
}

}  // namespace host
}  // namespace sm

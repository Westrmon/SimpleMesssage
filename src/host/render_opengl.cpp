// SimpleMessage — OpenGL 3.3 渲染后端（回退）

#include "host/render_backend.h"

#include <new>

// GLFW 会先把 APIENTRY / WINGDIAPI 定义好再引入系统的 GL 头，
// 所以这里不要再自己包含 <windows.h> 或 <GL/gl.h>。
#include "GLFW/glfw3.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "host/log.h"
#include "platform/window.h"

namespace sm {
namespace host {
namespace {

constexpr const char* kGlslVersion = "#version 330 core";

class OpenGlBackend final : public IRenderBackend {
 public:
  bool Init(platform::window::IWindow* window) override {
    if (window_ != nullptr) return true;
    if (window == nullptr || window->native_handle() == nullptr) {
      sm::log::Write(sm::LogLevel::Error, "ui", "没有窗口，无法建立 OpenGL 后端");
      return false;
    }
    if (window->client_api() != platform::window::ClientApi::kOpenGl) {
      sm::log::Write(sm::LogLevel::Error, "ui",
                     "窗口没有 OpenGL 上下文，无法使用 OpenGL 后端");
      return false;
    }
    window_ = window;
    MakeContextCurrent();

    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
      sm::log::Write(sm::LogLevel::Error, "ui", "imgui 的 OpenGL3 后端初始化失败");
      window_ = nullptr;
      return false;
    }
    imgui_ready_ = true;

    glfwSwapInterval(1);

    const GLubyte* version = glGetString(GL_VERSION);
    sm::log::Writef(sm::LogLevel::Info, "ui", "OpenGL 已就绪（%s）",
                    version != nullptr ? reinterpret_cast<const char*>(version) : "版本未知");
    return true;
  }

  void Shutdown() override {
    if (window_ == nullptr) return;
    if (imgui_ready_) {
      ImGui_ImplOpenGL3_Shutdown();
      imgui_ready_ = false;
    }
    window_ = nullptr;
  }

  void NewFrame() override {
    // 每张表面有自己的 ImGui 上下文；NewFrame 会在当前 GL 上下文里创建/更新
    // 后端设备对象，因此必须先切回本表面的上下文。
    MakeContextCurrent();
    ImGui_ImplOpenGL3_NewFrame();
  }

  bool BeginFrame(const float clear_rgba[4]) override {
    if (window_ == nullptr) return false;

    MakeContextCurrent();

    int width = 0;
    int height = 0;
    window_->ClientSize(&width, &height);
    if (width <= 0 || height <= 0) return false;

    glViewport(0, 0, width, height);
    glClearColor(clear_rgba[0], clear_rgba[1], clear_rgba[2], clear_rgba[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
  }

  void RenderDrawData() override {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  bool Present() override {
    if (window_ == nullptr) return false;
    glfwSwapBuffers(static_cast<GLFWwindow*>(window_->native_handle()));
    return true;
  }

  void MakeCurrent() override { MakeContextCurrent(); }

  std::uint64_t CreateTextureRgba(const void* rgba, int width, int height) override {
    if (window_ == nullptr || rgba == nullptr || width <= 0 || height <= 0) return 0;

    MakeContextCurrent();
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) return 0;

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // GL_CLAMP_TO_EDGE；Windows 的 GL/gl.h 只到 1.1，未必定义这个 1.2 常量。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<std::uint64_t>(texture);
  }

  void DestroyTexture(std::uint64_t texture) override {
    if (texture == 0 || window_ == nullptr) return;
    MakeContextCurrent();
    GLuint id = static_cast<GLuint>(texture);
    glDeleteTextures(1, &id);
  }

  bool alive() const override { return window_ != nullptr; }

  const char* name() const override { return "OpenGL"; }

 private:
  void MakeContextCurrent() {
    if (window_ != nullptr) {
      glfwMakeContextCurrent(static_cast<GLFWwindow*>(window_->native_handle()));
    }
  }

  platform::window::IWindow* window_ = nullptr;
  bool                       imgui_ready_ = false;
};

}  // namespace

IRenderBackend* CreateOpenGlBackend() { return new (std::nothrow) OpenGlBackend(); }

}  // namespace host
}  // namespace sm

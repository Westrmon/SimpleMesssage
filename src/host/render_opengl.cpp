// SimpleMessage — OpenGL 3.3 渲染后端（回退）

#include "host/render_backend.h"

#include <new>

#include <windows.h>
#include <GL/gl.h>

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

  void NewFrame() override { ImGui_ImplOpenGL3_NewFrame(); }

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

// SimpleMessage — imgui 根节点的实现
#include "host/ui_root.h"

namespace sm {
namespace host {

RootWindow::RootWindow(const char* id) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();

  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus;

  visible_ = ImGui::Begin(id, nullptr, flags);
}

RootWindow::~RootWindow() { ImGui::End(); }

}  // namespace host
}  // namespace sm

// SimpleMessage — 一扇窗口的 imgui 根节点
#pragma once

#include "imgui.h"

namespace sm {
namespace host {

inline constexpr const char* kPanelRootId = "##panel_root";

class RootWindow {
 public:
  explicit RootWindow(const char* id);
  ~RootWindow();

  RootWindow(const RootWindow&) = delete;
  RootWindow& operator=(const RootWindow&) = delete;

  bool visible() const { return visible_; }

 private:
  bool visible_ = false;
};

}  // namespace host
}  // namespace sm

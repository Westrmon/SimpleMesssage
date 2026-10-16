// SimpleMessage — 托盘图标与右键菜单（外壳的 Win32 那一半）
#pragma once

#include "sm/notification.h"

namespace sm {
namespace platform {
namespace win32 {

struct TrayLabels {
  const char* tooltip = "";
  const char* open_panel = "";
  const char* pause = "";
  const char* open_config_dir = "";
  const char* open_log_dir = "";
  const char* about = "";
  const char* exit = "";
};

class ITrayHost {
 public:
  virtual ~ITrayHost() = default;

  virtual bool pause_checked() const = 0;

  virtual void OnOpenPanel() = 0;
  virtual void OnTogglePause() = 0;
  virtual void OnOpenConfigDir() = 0;
  virtual void OnOpenLogDir() = 0;
  virtual void OnAbout() = 0;
  virtual void OnExit() = 0;
};

class ITray {
 public:
  virtual ~ITray() = default;

  virtual bool Create(const TrayLabels& labels, ITrayHost* host) = 0;
  virtual void Destroy() = 0;

  virtual void SetLevel(const sm::Level* level) = 0;
  virtual void ShowBalloon(const char* title_utf8, const char* text_utf8) = 0;
};

ITray* create_tray();

}  // namespace win32
}  // namespace platform
}  // namespace sm

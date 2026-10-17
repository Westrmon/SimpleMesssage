// SimpleMessage — 控制面板的状态持久化
#pragma once

#include <cstdint>
#include <string>

#include "sm/notification.h"

#include "host/config.h"
#include "platform/window.h"

namespace sm {
namespace host {

enum class PanelPage : std::int32_t {
  Overview = 0,
  Plugins = 1,
  Notifications = 2,
  Rules = 3,
  Routing = 4,
  Log = 5,
  About = 6,
};

inline constexpr int kPanelPageCount = 7;

const char* PanelPageKey(PanelPage page);
PanelPage   PanelPageFromKey(const char* key, PanelPage fallback);

inline constexpr sm::Level kNotifyLevels[] = {
    sm::Level::Debug, sm::Level::Info, sm::Level::Warning, sm::Level::Error,
    sm::Level::Critical,
};
inline constexpr int kNotifyLevelCount = 5;

inline constexpr sm::LogLevel kLogLevels[] = {
    sm::LogLevel::Trace, sm::LogLevel::Debug, sm::LogLevel::Info,
    sm::LogLevel::Warn,  sm::LogLevel::Error,
};
inline constexpr int kLogLevelCount = 5;

const char* LevelKey(sm::Level level);
const char* LogLevelKey(sm::LogLevel level);

class LevelMask {
 public:
  static constexpr std::uint32_t kAll = 0x1Fu;
  static constexpr std::uint32_t kNotifyDefault = kAll;
  static constexpr std::uint32_t kLogDefault = 0x1Cu;

  std::uint32_t mask = kAll;

  bool Has(sm::Level level) const {
    return (mask & (1u << static_cast<std::uint32_t>(level))) != 0;
  }
  bool Has(sm::LogLevel level) const {
    return (mask & (1u << static_cast<std::uint32_t>(level))) != 0;
  }

  void Set(int level_index, bool on) {
    const std::uint32_t bit = (level_index >= 0 && level_index < 32)
                                  ? (1u << static_cast<std::uint32_t>(level_index))
                                  : 0u;
    if (on) {
      mask |= bit;
    } else {
      mask &= ~bit;
    }
  }
  bool empty() const { return mask == 0; }
};

struct NotifyFilter {
  LevelMask   levels = LevelMask{LevelMask::kNotifyDefault};
  std::string source;
  std::string topic;
  std::string text;
};

struct LogFilter {
  LevelMask   levels = LevelMask{LevelMask::kLogDefault};
  std::string source;
  std::string text;
  bool        follow = true;
  bool        paused = false;
};

struct UiState {
  static constexpr int kUseSystemPosition = platform::window::kUnsetPosition;
  int x = kUseSystemPosition;
  int y = kUseSystemPosition;
  int w = 0;
  int h = 0;

  PanelPage last_page = PanelPage::Overview;
  bool close_hint_shown = false;

  NotifyFilter notify;
  LogFilter    log;
};

std::string UiStatePath(const config::Paths& paths);

enum class UiStateLoad : std::int32_t {
  Ok = 0,
  Missing = 1,
  TooNew = 2,
  Corrupt = 3,
};

UiStateLoad LoadUiState(const std::string& file_path, UiState* out,
                        std::string* out_error = nullptr);

bool SaveUiState(const std::string& file_path, const UiState& state,
                 std::string* out_error = nullptr);

inline bool UiStateWritable(UiStateLoad result) { return result != UiStateLoad::TooNew; }

}  // namespace host
}  // namespace sm

// SimpleMessage 示例插件 —— notifier.sound
//
// 定位：一个只做一件事的 Notifier —— 通知到达时按级别播放提示音。
// 它不画任何 UI、不创建窗口、不启动线程；on_notify 里唯一做的事是把一段
// 声音交给 Windows 的 PlaySound（SND_ASYNC），后者立即返回，播放发生在系统
// 自己的线程里。这满足 Notifier 的两条硬规则：主线程 + 禁止阻塞。
//
// 每档级别可以各自指定：
//   * 系统声音别名（如 SystemAsterisk / SystemExclamation / SystemHand）；
//   * 一个 WAV 文件路径（以 .wav 结尾或包含路径分隔符）。
// 空字符串表示这一档不发声。
//
// 平台说明：声音后端目前只有 Windows（winmm）。非 Windows 构建仍可编译，
// 但会在初始化时记一条 Warn，而不是静默地什么都不做。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _WIN32
#include <windows.h>

#include <mmsystem.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"

namespace {

using sm::LogLevel;
using sm::PluginBase;
using sm::PluginKind;
using sm::PluginMeta;
using sm::SettingDef;
using sm::SettingOption;

constexpr SettingOption kMinLevelOptions[] = {
    {"debug", "Debug 及以上（全部）"},
    {"info", "Info 及以上"},
    {"warning", "Warning 及以上"},
    {"error", "Error 及以上"},
    {"critical", "仅 Critical"},
};

constexpr SettingDef kSettings[] = {
    sm::setting::boolean("enabled", true, "启用声音提醒",
                         "关掉后本插件不播放任何声音，通知照常由其它 Notifier 处理。"),
    sm::setting::enumeration("min_level", "info", sm::span_of(kMinLevelOptions),
                             "最低播放级别",
                             "低于该级别的通知不发声。默认 Info 及以上；"
                             "Debug 通常是轮询类插件的常态输出，默认不出声。"),
    sm::setting::text("sound_debug", "", 512, "Debug 提示音",
                      "留空 = 不发声。其它级别可填系统声音别名（如 SystemAsterisk）"
                      "或 WAV 文件路径。"),
    sm::setting::text("sound_info", "SystemAsterisk", 512, "Info 提示音",
                      "系统声音别名或 WAV 文件路径；留空 = 不发声。"),
    sm::setting::text("sound_warning", "SystemExclamation", 512, "Warning 提示音",
                      "系统声音别名或 WAV 文件路径；留空 = 不发声。"),
    sm::setting::text("sound_error", "SystemHand", 512, "Error 提示音",
                      "系统声音别名或 WAV 文件路径；留空 = 不发声。"),
    sm::setting::text("sound_critical", "SystemHand", 512, "Critical 提示音",
                      "系统声音别名或 WAV 文件路径；留空 = 不发声。"
                      "默认与 Error 相同，但可以换成更醒目的声音。"),
    sm::setting::integer("throttle_ms", 500, 0, 60000, "最小播放间隔（毫秒）",
                         "两次声音之间至少间隔这么久；间隔内到达的同级或更低级别"
                         "通知不出声（更高的级别仍会播放，通知本身不受影响）。"
                         "0 = 不限制。默认 500 ms，避免短时间内的多条通知把"
                         "声音叠成一团。"),
};

constexpr PluginMeta kMeta = {
    "notifier.sound",
    "声音提醒",
    "SimpleMessage 示例",
    "1.0.0",
    "在通知到达时按级别播放系统提示音或自定义 WAV 文件。",
    PluginKind::Notifier,
    ">=0.1.0,<1.0.0",
    sm::Span<const char*>{},
    sm::span_of(kSettings),
    SM_API_VERSION,
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

bool EndsWithWav(const std::string& s) {
  if (s.size() < 4) return false;
  char tail[4];
  for (std::size_t i = 0; i < 4; ++i) {
    char c = s[s.size() - 4 + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    tail[i] = c;
  }
  return tail[0] == '.' && tail[1] == 'w' && tail[2] == 'a' && tail[3] == 'v';
}

// 含路径分隔符或以 .wav 结尾的，按文件处理；其余按系统声音别名处理。
bool LooksLikePath(const std::string& s) {
  return s.find('/') != std::string::npos || s.find('\\') != std::string::npos ||
         EndsWithWav(s);
}

int MinLevelThreshold(const char* value) {
  if (value != nullptr) {
    if (std::strcmp(value, "debug") == 0) return static_cast<int>(sm::Level::Debug);
    if (std::strcmp(value, "info") == 0) return static_cast<int>(sm::Level::Info);
    if (std::strcmp(value, "warning") == 0) return static_cast<int>(sm::Level::Warning);
    if (std::strcmp(value, "error") == 0) return static_cast<int>(sm::Level::Error);
    if (std::strcmp(value, "critical") == 0) return static_cast<int>(sm::Level::Critical);
  }
  return static_cast<int>(sm::Level::Info);
}

const char* LevelSettingName(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return "sound_debug";
    case sm::Level::Info:     return "sound_info";
    case sm::Level::Warning:  return "sound_warning";
    case sm::Level::Error:    return "sound_error";
    case sm::Level::Critical: return "sound_critical";
  }
  return "sound_info";
}

const char* LevelDefaultSound(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return "";
    case sm::Level::Info:     return "SystemAsterisk";
    case sm::Level::Warning:  return "SystemExclamation";
    case sm::Level::Error:    return "SystemHand";
    case sm::Level::Critical: return "SystemHand";
  }
  return "";
}

const char* LevelName(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return "Debug";
    case sm::Level::Info:     return "Info";
    case sm::Level::Warning:  return "Warning";
    case sm::Level::Error:    return "Error";
    case sm::Level::Critical: return "Critical";
  }
  return "?";
}

#ifdef _WIN32

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                       static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0) return {};
  std::wstring out(static_cast<std::size_t>(need), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          out.data(), need) <= 0) {
    return {};
  }
  return out;
}

bool IsAbsolutePath(const std::wstring& path) {
  if (path.size() >= 2 && path[1] == L':') return true;  // C:\...
  return !path.empty() && (path[0] == L'\\' || path[0] == L'/');  // \\server 或 \dir
}

// 相对路径按宿主的配置目录解析。
std::wstring ResolvePath(const std::string& spec, const char* config_dir) {
  std::wstring path = Utf8ToWide(spec);
  if (path.empty() || IsAbsolutePath(path)) return path;

  const std::wstring base = (config_dir != nullptr) ? Utf8ToWide(config_dir) : std::wstring();
  if (base.empty()) return path;
  std::wstring out = base;
  if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
  out += path;
  return out;
}

bool IsReadableFile(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

#endif  // _WIN32

class SoundNotifier final : public PluginBase {
 public:
  explicit SoundNotifier(sm::IHostContext* host) : PluginBase(host, kMeta) {}
  ~SoundNotifier() override { StopSound(); }

  bool initialize() override {
#ifndef _WIN32
    if (!warned_platform_) {
      warned_platform_ = true;
      host()->log(LogLevel::Warn,
                  "notifier.sound 目前只在 Windows 上播放声音，本平台不会发声。");
    }
#endif
    return true;
  }

  void on_notify(const sm::Notification& n) override {
    if (!host()->setting_bool("enabled", true)) return;
    if (static_cast<int>(n.level) <
        MinLevelThreshold(host()->setting_enum("min_level", "info"))) {
      return;
    }

    const std::int64_t now = host()->now_unix_ms();
    const std::int64_t throttle =
        Clamp(host()->setting_int("throttle_ms", 500), 0, 60000);
    // 节流只压同级或更低级别：刚响过 Info 时来了一条 Critical，仍然要出声。
    if (throttle > 0 && last_played_ms_ != 0 &&
        now - last_played_ms_ < throttle &&
        static_cast<int>(n.level) <= static_cast<int>(last_played_level_)) {
      return;
    }

    const char* const setting_value =
        host()->setting_string(LevelSettingName(n.level), LevelDefaultSound(n.level));
    const std::string spec = Trim(setting_value != nullptr ? setting_value : "");
    if (spec.empty()) return;

    if (Play(spec)) {
      last_played_ms_ = now;
      last_played_level_ = n.level;
      host()->logf(LogLevel::Debug, "播放 %s 提示音：%s", LevelName(n.level),
                   spec.c_str());
    }
  }

  void stop() override {
    StopSound();
    last_played_ms_ = 0;
    last_played_level_ = sm::Level::Debug;
  }
  void exit() override { StopSound(); }

 private:
  static std::int64_t Clamp(std::int64_t v, std::int64_t lo, std::int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  }

#ifdef _WIN32
  bool Play(const std::string& spec) {
    const bool is_file = LooksLikePath(spec);
    const std::wstring target =
        is_file ? ResolvePath(spec, host()->config_dir()) : Utf8ToWide(spec);
    if (target.empty()) {
      WarnOnce(spec, "提示音设置不是合法的 UTF-8 文本");
      return false;
    }
    if (is_file) {
      // PlaySound 在异步模式下即使文件不存在也会返回 TRUE（失败发生在系统
      // 自己的线程里），所以自己先确认一次。结果按 spec 缓存，之后不再做
      // 文件系统调用 —— on_notify 不允许因为一条通知去等磁盘。
      if (spec != checked_file_spec_) {
        checked_file_spec_ = spec;
        file_ok_ = IsReadableFile(target);
      }
      if (!file_ok_) {
        WarnOnce(spec, "WAV 文件不存在或不可读");
        return false;
      }
    }

    DWORD flags = SND_ASYNC | SND_NODEFAULT;
    flags |= is_file ? SND_FILENAME : SND_ALIAS;
    if (!PlaySoundW(target.c_str(), nullptr, flags)) {
      WarnOnce(spec, is_file ? "WAV 文件不存在、不可读或格式不受支持"
                             : "系统没有这个声音别名");
      return false;
    }
    last_failed_spec_.clear();
    return true;
  }

  static void StopSound() { PlaySoundW(nullptr, nullptr, 0); }

  void WarnOnce(const std::string& spec, const char* reason) {
    if (spec == last_failed_spec_) return;
    last_failed_spec_ = spec;
    host()->logf(LogLevel::Warn, "无法播放提示音 \"%s\"：%s。", spec.c_str(), reason);
  }
#else
  bool Play(const std::string&) { return false; }
  static void StopSound() {}
#endif

  // on_notify / stop / exit 都在主线程，因此下面这些成员不需要加锁。
  std::int64_t last_played_ms_ = 0;
  sm::Level last_played_level_ = sm::Level::Debug;
  std::string last_failed_spec_;
  std::string checked_file_spec_;
  bool file_ok_ = false;
#ifndef _WIN32
  bool warned_platform_ = false;
#endif
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, SoundNotifier);

// SimpleMessage 示例插件 —— notifier.sound
//
// 播放由 miniaudio 完成：WAV / MP3 直接解码输出，不依赖平台的系统播放
// API，因此同一份代码在 Windows / Linux / macOS 上行为一致。

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "miniaudio.h"

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
    sm::setting::integer("volume_percent", 100, 0, 130, "音量（%）",
                         "本插件播放通知音时的音量。0 = 静音，100 = 原始音量；"
                         "不改变系统音量，也不影响其他程序。"),
    sm::setting::sound("sound_debug", "", 512, "Debug 提示音",
                       "WAV 或 MP3 文件路径，留空 = 不发声。相对路径按配置目录解析。"
                       "也可以点“浏览…”选文件、点“试听”先听。"),
    sm::setting::sound("sound_info", "", 512, "Info 提示音",
                       "WAV 或 MP3 文件路径，留空 = 不发声。相对路径按配置目录解析。"
                       "“浏览…”选文件，“试听”立即播放。"),
    sm::setting::sound("sound_warning", "", 512, "Warning 提示音",
                       "WAV 或 MP3 文件路径，留空 = 不发声。相对路径按配置目录解析。"
                       "“浏览…”选文件，“试听”立即播放。"),
    sm::setting::sound("sound_error", "", 512, "Error 提示音",
                       "WAV 或 MP3 文件路径，留空 = 不发声。相对路径按配置目录解析。"
                       "“浏览…”选文件，“试听”立即播放。"),
    sm::setting::sound("sound_critical", "", 512, "Critical 提示音",
                       "WAV 或 MP3 文件路径，留空 = 不发声。相对路径按配置目录解析。"
                       "可以换一个比 Error 更醒目的声音。"),
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
    "1.2.0",
    "在通知到达时按级别播放 WAV / MP3 文件。",
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

char Lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EndsWithIgnoreCase(const std::string& s, const char* suffix) {
  const std::size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (std::size_t i = 0; i < n; ++i) {
    if (Lower(s[s.size() - n + i]) != suffix[i]) return false;
  }
  return true;
}

// 只接受 WAV / MP3。系统声音别名（如 SystemAsterisk）没有扩展名，
// 也因此在这里被挡下 —— miniaudio 不认识它们。
bool IsSupportedFile(const std::string& spec) {
  return EndsWithIgnoreCase(spec, ".wav") || EndsWithIgnoreCase(spec, ".mp3");
}

bool IsAbsolutePath(const std::string& path) {
#ifdef _WIN32
  if (path.size() >= 2 && path[1] == ':') return true;  // C:\...
  return !path.empty() && (path[0] == '\\' || path[0] == '/');
#else
  return !path.empty() && path[0] == '/';
#endif
}

// 相对路径按宿主的配置目录解析。
std::string ResolvePath(const std::string& spec, const char* config_dir) {
  if (IsAbsolutePath(spec) || config_dir == nullptr || config_dir[0] == '\0') {
    return spec;
  }
  std::string out = config_dir;
  if (out.back() != '/' && out.back() != '\\') out.push_back('/');
  out += spec;
  return out;
}

// 本项目构建 miniaudio 时定义了 MA_NO_RESOURCE_MANAGER（只留最直接的
// 播放路径），因此声音不能从文件路径直接创建，而是先打开解码器，再把
// 解码器当作数据源挂到引擎上。解码器必须比 ma_sound 活得久。
ma_result InitDecoder(const std::string& path, ma_decoder* decoder) {
  const ma_decoder_config config = ma_decoder_config_init_default();
#ifdef _WIN32
  // miniaudio 的窄路径版本在 Windows 上按当前代码页打开文件，中文路径会失败；
  // 所以 Windows 上始终走宽字符版本。
  const std::wstring wide = std::filesystem::u8path(path).wstring();
  if (wide.empty()) return MA_INVALID_ARGS;
  return ma_decoder_init_file_w(wide.c_str(), &config, decoder);
#else
  return ma_decoder_init_file(path.c_str(), &config, decoder);
#endif
}

ma_result InitSoundFromDecoder(ma_engine* engine, ma_decoder* decoder,
                               ma_sound* sound) {
  constexpr ma_uint32 kFlags =
      MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
  return ma_sound_init_from_data_source(engine, decoder, kFlags, nullptr, sound);
}

const char* FailureReason(ma_result result) {
  switch (result) {
    case MA_DOES_NOT_EXIST: return "文件不存在";
    case MA_ACCESS_DENIED:  return "文件不可读（权限不足）";
    case MA_INVALID_FILE:   return "文件内容不是有效的 WAV / MP3";
    case MA_NOT_IMPLEMENTED: return "格式不受支持（只支持 WAV 与 MP3）";
    case MA_OUT_OF_MEMORY:  return "内存不足";
    default:                return "无法解码文件或音频设备不可用";
  }
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

class SoundNotifier final : public PluginBase {
 public:
  explicit SoundNotifier(sm::IHostContext* host) : PluginBase(host, kMeta) {}
  ~SoundNotifier() override { StopWorker(); }

  bool initialize() override {
    StartWorker();
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
        host()->setting_string(LevelSettingName(n.level), "");
    const std::string spec = Trim(setting_value != nullptr ? setting_value : "");
    if (spec.empty()) return;

    // 解码与等待播放结束都在后台线程，on_notify 只负责把最新一条交出去。
    Queue(spec);
    last_played_ms_ = now;
    last_played_level_ = n.level;
    host()->logf(LogLevel::Debug, "播放 %s 提示音：%s", LevelName(n.level),
                 spec.c_str());
  }

  void stop() override {
    StopWorker();
    last_played_ms_ = 0;
    last_played_level_ = sm::Level::Debug;
  }
  void exit() override { StopWorker(); }

 private:
  static std::int64_t Clamp(std::int64_t v, std::int64_t lo, std::int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  }

  void StartWorker() {
    if (worker_.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      worker_stop_ = false;
      pending_spec_.clear();
      pending_seq_ = 0;
      handled_seq_ = 0;
    }
    worker_ = std::thread([this] { WorkerLoop(); });
  }

  void StopWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      worker_stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    // 重新启用插件时再试一次（设备可能已经接上了）。
    engine_failed_ = false;
    last_failed_spec_.clear();
  }

  // 只保留最新一条：连续通知时旧声音被新声音打断，与单路播放的语义一致。
  void Queue(const std::string& spec) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_spec_ = spec;
      ++pending_seq_;
    }
    cv_.notify_one();
  }

  void WorkerLoop() {
    for (;;) {
      std::string spec;
      std::uint64_t seq = 0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
          return worker_stop_ || pending_seq_ != handled_seq_;
        });
        if (worker_stop_) break;
        spec = pending_spec_;
        seq = pending_seq_;
        handled_seq_ = seq;
      }
      PlayBlocking(spec, seq);
    }

    if (engine_ready_) {
      ma_engine_uninit(&engine_);
      engine_ready_ = false;
    }
  }

  // 在后台线程上同步解码并播放，直到播完、被新通知打断或插件停用。
  void PlayBlocking(const std::string& spec, std::uint64_t seq) {
    if (!IsSupportedFile(spec)) {
      WarnOnce(spec, "只支持 WAV / MP3 文件路径（不再支持系统声音别名）");
      return;
    }
    if (!EnsureEngine()) return;

    const std::string path = ResolvePath(spec, host()->config_dir());
    ma_decoder decoder;
    ma_result result = InitDecoder(path, &decoder);
    if (result != MA_SUCCESS) {
      WarnOnce(spec, FailureReason(result));
      return;
    }

    ma_sound sound;
    result = InitSoundFromDecoder(&engine_, &decoder, &sound);
    if (result != MA_SUCCESS) {
      ma_decoder_uninit(&decoder);
      WarnOnce(spec, FailureReason(result));
      return;
    }

    const std::int64_t volume =
        Clamp(host()->setting_int("volume_percent", 100), 0, 130);
    ma_sound_set_volume(&sound, static_cast<float>(volume) / 100.0f);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (worker_stop_ || pending_seq_ != seq) {
        ma_sound_uninit(&sound);
        ma_decoder_uninit(&decoder);
        return;
      }
    }

    if (ma_sound_start(&sound) != MA_SUCCESS) {
      ma_sound_uninit(&sound);
      ma_decoder_uninit(&decoder);
      WarnOnce(spec, "无法开始播放");
      return;
    }

    host()->logf(LogLevel::Debug, "提示音已开始播放：%s", path.c_str());

    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (!worker_stop_ && pending_seq_ == seq && !ma_sound_at_end(&sound)) {
        cv_.wait_for(lock, std::chrono::milliseconds(20));
      }
    }
    ma_sound_uninit(&sound);
    ma_decoder_uninit(&decoder);
    last_failed_spec_.clear();
  }

  bool EnsureEngine() {
    if (engine_ready_) return true;
    if (engine_failed_) return false;

    const ma_result result = ma_engine_init(nullptr, &engine_);
    if (result != MA_SUCCESS) {
      engine_failed_ = true;
      host()->logf(LogLevel::Warn, "无法初始化音频输出：%s。",
                   ma_result_description(result));
      return false;
    }
    engine_ready_ = true;
    return true;
  }

  void WarnOnce(const std::string& spec, const char* reason) {
    if (spec == last_failed_spec_) return;
    last_failed_spec_ = spec;
    host()->logf(LogLevel::Warn, "无法播放提示音 \"%s\"：%s。", spec.c_str(), reason);
  }

  // on_notify / stop / exit 都在主线程，因此下面这些成员不需要加锁。
  std::int64_t last_played_ms_ = 0;
  sm::Level last_played_level_ = sm::Level::Debug;

  // 播放线程的状态。
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::string pending_spec_;
  std::uint64_t pending_seq_ = 0;
  std::uint64_t handled_seq_ = 0;
  bool worker_stop_ = false;

  // 只在播放线程里访问。
  ma_engine engine_{};
  bool engine_ready_ = false;
  bool engine_failed_ = false;
  std::string last_failed_spec_;
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, SoundNotifier);

// SimpleMessage — 平台层：提示音试听
//
// 与 notifier.sound 共用 miniaudio：WAV / MP3 直接解码，路径处理之外没有
// 任何平台专有代码。试听是用户点出来的单次动作，所以允许在这里同步打开
// 文件；真正的播放仍在音频线程上进行，不会卡住界面。

#include "platform/sound.h"

#include <cstring>
#include <string>

#include "miniaudio.h"

#ifdef _WIN32
#include "platform/utf8.h"
#endif

namespace sm {
namespace platform {
namespace {

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(const std::string& s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && IsSpace(s[begin])) ++begin;
  while (end > begin && IsSpace(s[end - 1])) --end;
  return s.substr(begin, end - begin);
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

std::string ResolvePath(const std::string& spec, const std::string& base_dir) {
  if (IsAbsolutePath(spec) || base_dir.empty()) return spec;
  std::string out = base_dir;
  if (out.back() != '/' && out.back() != '\\') out.push_back('/');
  out += spec;
  return out;
}

// miniaudio 在本项目的构建里关闭了资源管理器（MA_NO_RESOURCE_MANAGER），
// 所以先打开解码器，再把解码器当作数据源挂到引擎上。
ma_result InitDecoder(const std::string& path, ma_decoder* decoder) {
  const ma_decoder_config config = ma_decoder_config_init_default();
#ifdef _WIN32
  // 窄路径在 Windows 上按当前代码页打开，中文路径会失败；改用宽字符版本。
  const std::wstring wide = utf8::widen(path.c_str());
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

void SetError(std::string* error, const char* text) {
  if (error != nullptr) *error = text;
}

// 引擎与当前试听的声音常驻到下一次试听或进程退出：试听没有"停止"按钮，
// 单路替换的语义与原先的 PlaySound 一致。
struct PreviewState {
  ma_engine engine{};
  ma_sound sound{};
  ma_decoder decoder{};
  bool engine_ready = false;
  bool engine_failed = false;
  bool sound_ready = false;
  bool decoder_ready = false;

  ~PreviewState() {
    ResetSound();
    if (engine_ready) ma_engine_uninit(&engine);
  }

  // ma_sound 先于它引用的解码器销毁。
  void ResetSound() {
    if (sound_ready) {
      ma_sound_uninit(&sound);
      sound_ready = false;
    }
    if (decoder_ready) {
      ma_decoder_uninit(&decoder);
      decoder_ready = false;
    }
  }
};

PreviewState g_preview;

}  // namespace

bool PreviewSound(const std::string& spec, const std::string& base_dir,
                  std::string* error) {
  const std::string value = Trim(spec);
  if (value.empty()) {
    SetError(error, "没有填写声音（留空表示不发声）");
    return false;
  }
  if (!IsSupportedFile(value)) {
    SetError(error, "只支持 WAV 与 MP3 文件");
    return false;
  }

  g_preview.ResetSound();

  if (!g_preview.engine_ready) {
    if (g_preview.engine_failed) {
      SetError(error, "音频设备不可用");
      return false;
    }
    const ma_result result = ma_engine_init(nullptr, &g_preview.engine);
    if (result != MA_SUCCESS) {
      g_preview.engine_failed = true;
      SetError(error, ma_result_description(result));
      return false;
    }
    g_preview.engine_ready = true;
  }

  ma_result result =
      InitDecoder(ResolvePath(value, base_dir), &g_preview.decoder);
  if (result != MA_SUCCESS) {
    SetError(error, FailureReason(result));
    return false;
  }
  g_preview.decoder_ready = true;

  result = InitSoundFromDecoder(&g_preview.engine, &g_preview.decoder,
                                &g_preview.sound);
  if (result != MA_SUCCESS) {
    g_preview.ResetSound();
    SetError(error, FailureReason(result));
    return false;
  }
  g_preview.sound_ready = true;

  if (ma_sound_start(&g_preview.sound) != MA_SUCCESS) {
    g_preview.ResetSound();
    SetError(error, "无法开始播放");
    return false;
  }
  return true;
}

}  // namespace platform
}  // namespace sm

// SimpleMessage — 通知图标的下载、解码与缓存
#include "host/image_cache.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// windows.h 的 min/max 宏会破坏 <algorithm> 里的 std::min / std::max。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#ifdef SM_HAVE_CURL
#include <curl/curl.h>
#endif

#include "platform/platform.h"

#include "host/log.h"
#include "host/ui_session.h"
#include "host/util.h"

using Microsoft::WRL::ComPtr;

namespace sm {
namespace host {
namespace {

constexpr std::int64_t kPruneIntervalMs = 60 * 1000;
constexpr std::int64_t kEntryIdleMs = 10 * 60 * 1000;
constexpr std::size_t  kMaxEntries = 128;
constexpr std::size_t  kMaxDownloadBytes = 8u * 1024u * 1024u;
constexpr std::size_t  kMaxFileBytes = 8u * 1024u * 1024u;
// 解码前先看原始尺寸：一张 8192x8192 的图会先吃掉 256MB 内存，再缩放已经晚了。
constexpr unsigned int kMaxSourcePixels = 4096u * 4096u;
constexpr int kMaxDecodedDimension = 256;
// 同时进行的下载/解码上限。一条通知风暴里可能有几百个不同来源，
// 一个来源一条线程会在几秒内造出几百条线程。
constexpr int kMaxConcurrentLoads = 4;

bool StartsWithNoCase(const std::string& text, const char* prefix) {
  const std::size_t length = std::strlen(prefix);
  if (text.size() < length) return false;
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char a = static_cast<unsigned char>(text[i]);
    const unsigned char b = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

bool IsAbsolutePath(const std::string& path) {
  if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':') {
    return true;
  }
  if (path.size() >= 2 && (path[0] == '\\' || path[0] == '/') &&
      (path[1] == '\\' || path[1] == '/')) {
    return true;
  }
  return !path.empty() && (path[0] == '\\' || path[0] == '/');
}

std::string UrlDecode(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hex(text[i + 1]);
      const int lo = hex(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

std::string ResolveLocalPath(const std::string& source, const std::string& data_dir) {
  std::string path = source;
  if (StartsWithNoCase(path, "file://")) {
    path = UrlDecode(path.substr(7));
    // file:///C:/a.png —— 去掉盘符前面多出来的那个斜杠。
    if (path.size() >= 3 && path[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
      path.erase(0, 1);
    }
  }
  if (IsAbsolutePath(path) || data_dir.empty()) return path;
  return util::path_join(data_dir, path);
}

std::string ShortSource(const std::string& source) {
  constexpr std::size_t kMaxLogSource = 200;
  if (source.size() <= kMaxLogSource) return source;
  return source.substr(0, kMaxLogSource) + "...";
}

bool ReadFileBytes(const std::string& utf8_path, std::vector<unsigned char>* out,
                   std::string* error) {
  FILE* file = nullptr;
  if (::_wfopen_s(&file, util::widen(utf8_path.c_str()).c_str(), L"rb") != 0 ||
      file == nullptr) {
    *error = "无法打开文件";
    return false;
  }

  bool ok = false;
  do {
    if (::fseek(file, 0, SEEK_END) != 0) {
      *error = "无法定位文件末尾";
      break;
    }
    const long size = ::ftell(file);
    if (size <= 0) {
      *error = "文件为空";
      break;
    }
    if (static_cast<std::size_t>(size) > kMaxFileBytes) {
      *error = "文件超过 8 MB";
      break;
    }
    if (::fseek(file, 0, SEEK_SET) != 0) {
      *error = "无法回到文件开头";
      break;
    }
    out->resize(static_cast<std::size_t>(size));
    if (::fread(out->data(), 1, out->size(), file) != out->size()) {
      *error = "读取文件失败";
      break;
    }
    ok = true;
  } while (false);

  ::fclose(file);
  return ok;
}

#ifdef SM_HAVE_CURL
std::once_flag g_curl_init_once;

std::size_t WriteDownloaded(char* data, std::size_t size, std::size_t count,
                            void* user) {
  auto* const body = static_cast<std::vector<unsigned char>*>(user);
  const std::size_t bytes = size * count;
  if (bytes > kMaxDownloadBytes || body->size() > kMaxDownloadBytes - bytes) {
    return 0;  // 超出上限，curl 会以 CURLE_WRITE_ERROR 中止
  }
  body->insert(body->end(), data, data + bytes);
  return bytes;
}

bool DownloadBytes(const std::string& url, std::vector<unsigned char>* out,
                   std::string* error) {
  std::call_once(g_curl_init_once,
                 [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

  CURL* const easy = curl_easy_init();
  if (easy == nullptr) {
    *error = "curl_easy_init 失败";
    return false;
  }

  char curl_error[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "SimpleMessage/0.1");
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteDownloaded);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curl_error);

  const CURLcode code = curl_easy_perform(easy);
  long status = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(easy);

  if (code != CURLE_OK) {
    if (code == CURLE_WRITE_ERROR && out->size() >= kMaxDownloadBytes) {
      *error = "下载内容超过 8 MB";
    } else if (curl_error[0] != '\0') {
      *error = curl_error;
    } else {
      *error = curl_easy_strerror(code);
    }
    return false;
  }
  if (status >= 400) {
    *error = "HTTP " + std::to_string(status);
    return false;
  }
  if (out->empty()) {
    *error = "下载内容为空";
    return false;
  }
  return true;
}
#endif  // SM_HAVE_CURL

struct DecodedImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> rgba;
};

bool DecodeImage(const std::vector<unsigned char>& bytes, DecodedImage* out,
                 std::string* error) {
  if (bytes.empty()) {
    *error = "图片数据为空";
    return false;
  }

  const HRESULT init = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  // RPC_E_CHANGED_MODE 表示这个线程已经以别的套间模型初始化过 COM —— WIC 仍然可用。
  const bool uninit = SUCCEEDED(init);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    *error = "COM 初始化失败";
    return false;
  }

  const auto finish = [uninit]() {
    if (uninit) ::CoUninitialize();
  };

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = ::CoCreateInstance(
      CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IWICImagingFactory),
      reinterpret_cast<void**>(factory.GetAddressOf()));
  if (FAILED(hr) || factory == nullptr) {
    *error = "创建 WIC 工厂失败";
    finish();
    return false;
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(stream.GetAddressOf());
  if (SUCCEEDED(hr)) {
    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
  }
  if (FAILED(hr)) {
    *error = "无法从内存建立图片流";
    finish();
    return false;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                        WICDecodeMetadataCacheOnDemand,
                                        decoder.GetAddressOf());
  if (FAILED(hr) || decoder == nullptr) {
    *error = "无法识别图片格式";
    finish();
    return false;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, frame.GetAddressOf());
  if (FAILED(hr) || frame == nullptr) {
    *error = "图片没有可解码的帧";
    finish();
    return false;
  }

  UINT source_width = 0;
  UINT source_height = 0;
  hr = frame->GetSize(&source_width, &source_height);
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    *error = "图片尺寸无效";
    finish();
    return false;
  }
  if (static_cast<unsigned long long>(source_width) * source_height >
      kMaxSourcePixels) {
    *error = "图片尺寸过大";
    finish();
    return false;
  }

  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(converter.GetAddressOf());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    *error = "图片像素格式转换失败";
    finish();
    return false;
  }

  int width = static_cast<int>(source_width);
  int height = static_cast<int>(source_height);
  ComPtr<IWICBitmapScaler> scaler;
  ComPtr<IWICBitmapSource> source = converter;
  if (width > kMaxDecodedDimension || height > kMaxDecodedDimension) {
    const double scale =
        std::min(static_cast<double>(kMaxDecodedDimension) / width,
                 static_cast<double>(kMaxDecodedDimension) / height);
    width = std::max(1, static_cast<int>(width * scale + 0.5));
    height = std::max(1, static_cast<int>(height * scale + 0.5));
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (SUCCEEDED(hr)) {
      hr = scaler->Initialize(converter.Get(), static_cast<UINT>(width),
                              static_cast<UINT>(height),
                              WICBitmapInterpolationModeFant);
    }
    if (FAILED(hr)) {
      *error = "图片缩放失败";
      finish();
      return false;
    }
    source = scaler;
  }

  const UINT stride = static_cast<UINT>(width) * 4u;
  out->width = width;
  out->height = height;
  out->rgba.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
  hr = source->CopyPixels(nullptr, stride, static_cast<UINT>(out->rgba.size()),
                          out->rgba.data());
  if (FAILED(hr)) {
    *error = "读取图片像素失败";
    out->rgba.clear();
    finish();
    return false;
  }

  finish();
  return true;
}

}  // namespace

struct ImageCache::Entry {
  enum class State { kLoading, kReady, kFailed };

  State                      state = State::kLoading;
  int                        width = 0;
  int                        height = 0;
  std::vector<unsigned char> rgba;
  std::int64_t               last_used_ms = 0;
};

struct ImageCache::State {
  std::mutex                                    mutex;
  std::map<std::string, std::shared_ptr<Entry>> entries;
  int                                           active_loads = 0;
};

ImageCache::ImageCache() : state_(std::make_shared<State>()) {}

ImageCache::~ImageCache() = default;

void ImageCache::SetDataDir(std::string dir) { data_dir_ = std::move(dir); }

void ImageCache::BeginFrame(std::int64_t now_ms) {
  const std::int64_t now = now_ms > 0 ? now_ms : platform::monotonic_ms();
  if (now - last_prune_ms_ < kPruneIntervalMs) return;
  last_prune_ms_ = now;

  std::lock_guard<std::mutex> lock(state_->mutex);
  PruneLocked(now);
}

void ImageCache::PruneLocked(std::int64_t now_ms) {
  for (auto it = state_->entries.begin(); it != state_->entries.end();) {
    if (it->second->state != Entry::State::kLoading &&
        now_ms - it->second->last_used_ms > kEntryIdleMs) {
      it = state_->entries.erase(it);
    } else {
      ++it;
    }
  }

  if (state_->entries.size() <= kMaxEntries) return;
  for (auto it = state_->entries.begin();
       it != state_->entries.end() && state_->entries.size() > kMaxEntries;) {
    if (it->second->state == Entry::State::kLoading) {
      ++it;
      continue;
    }
    it = state_->entries.erase(it);
  }
}

void ImageCache::LoadWorker(std::shared_ptr<State> state,
                            std::shared_ptr<Entry> entry, std::string source,
                            std::string data_dir) {
  std::vector<unsigned char> bytes;
  std::string error;
  DecodedImage decoded;

  bool ok = false;
  try {
    if (StartsWithNoCase(source, "http://") || StartsWithNoCase(source, "https://")) {
#ifdef SM_HAVE_CURL
      ok = DownloadBytes(source, &bytes, &error);
#else
      error = "宿主编译时没有网络支持（libcurl 不可用）";
#endif
    } else {
      const std::string path = ResolveLocalPath(source, data_dir);
      ok = ReadFileBytes(path, &bytes, &error);
    }

    if (ok) ok = DecodeImage(bytes, &decoded, &error);
    if (ok && decoded.rgba.empty()) ok = false;
  } catch (const std::exception& e) {
    // 这是 detached 线程：异常穿出去会 std::terminate 整个进程。
    error = e.what() != nullptr ? e.what() : "未知异常";
    ok = false;
  } catch (...) {
    error = "未知异常";
    ok = false;
  }

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    --state->active_loads;
    if (ok) {
      entry->state = Entry::State::kReady;
      entry->width = decoded.width;
      entry->height = decoded.height;
      entry->rgba = std::move(decoded.rgba);
      return;
    }
    entry->state = Entry::State::kFailed;
  }

  if (error.empty()) error = "未知错误";
  sm::log::Writef(sm::LogLevel::Warn, "ui", "图标加载失败（%s）：%s",
                  ShortSource(source).c_str(), error.c_str());
}

std::uint64_t ImageCache::Texture(const char* source, int* out_width,
                                  int* out_height, bool* out_failed) {
  if (out_width != nullptr) *out_width = 0;
  if (out_height != nullptr) *out_height = 0;
  if (out_failed != nullptr) *out_failed = false;
  if (source == nullptr || source[0] == '\0') return 0;

  const std::int64_t now = platform::monotonic_ms();
  std::shared_ptr<Entry> entry;
  bool start = false;

  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto it = state_->entries.find(source);
  if (it == state_->entries.end()) {
    entry = std::make_shared<Entry>();
    entry->last_used_ms = now;
    state_->entries.emplace(source, entry);
    start = true;
  } else {
    entry = it->second;
    entry->last_used_ms = now;
  }

  if (start) {
    if (state_->active_loads >= kMaxConcurrentLoads) {
      // 不留下"永远 Loading"的条目：下一帧还会走到这里重试。
      state_->entries.erase(source);
      return 0;
    }
    ++state_->active_loads;
    try {
      std::thread(&ImageCache::LoadWorker, state_, entry, std::string(source),
                  data_dir_)
          .detach();
    } catch (...) {
      --state_->active_loads;
      entry->state = Entry::State::kFailed;
    }
    return 0;
  }

  if (entry->state == Entry::State::kLoading) return 0;
  if (entry->state == Entry::State::kFailed) {
    if (out_failed != nullptr) *out_failed = true;
    return 0;
  }
  if (current_runtime_ == nullptr) return 0;

  if (out_width != nullptr) *out_width = entry->width;
  if (out_height != nullptr) *out_height = entry->height;
  return current_runtime_->AcquireTexture(source, entry->rgba.data(),
                                          entry->width, entry->height);
}

}  // namespace host
}  // namespace sm

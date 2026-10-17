// SimpleMessage — 通知图标：异步下载/解码 + 按 UI runtime 缓存纹理
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "host/plugin_host.h"

namespace sm {
namespace host {

class IUiRuntime;

// Notification::icon 的加载器。支持：
//   - http:// / https:// 网络图片（libcurl 下载，后台线程）
//   - file:// URL
//   - 本地图片路径；相对路径以 data_dir 为基准
// 下载与解码都在后台线程做，主线程只负责取结果、上传纹理。
// 失败结果会被缓存，不会对同一条来源反复重试。
class ImageCache final : public IIconProvider {
 public:
  ImageCache();
  ~ImageCache() override;
  ImageCache(const ImageCache&) = delete;
  ImageCache& operator=(const ImageCache&) = delete;

  void SetDataDir(std::string dir);
  void SetCurrentRuntime(IUiRuntime* runtime) { current_runtime_ = runtime; }
  void BeginFrame(std::int64_t now_ms);

  std::uint64_t Texture(const char* source, int* out_width, int* out_height,
                        bool* out_failed) override;

 private:
  struct Entry;
  struct State;

  static void LoadWorker(std::shared_ptr<State> state, std::shared_ptr<Entry> entry,
                         std::string source, std::string data_dir);
  void PruneLocked(std::int64_t now_ms);

  std::shared_ptr<State> state_;
  std::string            data_dir_;
  IUiRuntime*            current_runtime_ = nullptr;
  std::int64_t           last_prune_ms_ = 0;
};

}  // namespace host
}  // namespace sm

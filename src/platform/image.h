// SimpleMessage — 平台层：图像解码
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sm {
namespace platform {

struct DecodedImage {
  int                        width = 0;
  int                        height = 0;
  std::vector<unsigned char> rgba;
};

// 把 PNG/JPEG/BMP/ICO 等系统支持的格式解码成 RGBA8。
// max_dimension 限制输出边长；max_source_pixels 在解码前先拦截过大的图。
// 失败时返回 false，并把原因写进 error（若不为空）。
bool DecodeImageRgba(const std::vector<unsigned char>& bytes, int max_dimension,
                     std::uint64_t max_source_pixels, DecodedImage* out,
                     std::string* error);

}  // namespace platform
}  // namespace sm

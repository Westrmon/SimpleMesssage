// SimpleMessage — Win32 平台层：WIC 图像解码

#include "platform/image.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// windows.h 的 min/max 宏会破坏 <algorithm> 里的 std::min / std::max。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace sm {
namespace platform {
namespace {

bool decode_rgba(const std::vector<unsigned char>& bytes, int max_dimension,
                 std::uint64_t max_source_pixels, DecodedImage* out,
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
  if (static_cast<std::uint64_t>(source_width) * source_height > max_source_pixels) {
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
  if (width > max_dimension || height > max_dimension) {
    const double scale =
        std::min(static_cast<double>(max_dimension) / width,
                 static_cast<double>(max_dimension) / height);
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

bool DecodeImageRgba(const std::vector<unsigned char>& bytes, int max_dimension,
                     std::uint64_t max_source_pixels, DecodedImage* out,
                     std::string* error) {
  if (out == nullptr) return false;
  std::string local_error;
  std::string* error_out = (error != nullptr) ? error : &local_error;
  return decode_rgba(bytes, max_dimension, max_source_pixels, out, error_out);
}

}  // namespace platform
}  // namespace sm

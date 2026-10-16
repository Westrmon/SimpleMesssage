// SimpleMessage — 中文字体的实现（回退链 + imgui 图集装载）
#include "host/ui_fonts.h"

#include <windows.h>
#include <shlobj.h>

#include "imgui.h"

#include "host/log.h"
#include "host/util.h"

namespace sm {
namespace host {
namespace ui_fonts {
namespace {

const ImWchar* GlyphRanges(ImFontAtlas* atlas) {
  return atlas->GetGlyphRangesChineseSimplifiedCommon();
}

}  // namespace

std::string SystemFontDir() {
  wchar_t buffer[MAX_PATH] = {};
  const HRESULT hr = ::SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr,
                                        SHGFP_TYPE_CURRENT, buffer);
  if (FAILED(hr) || buffer[0] == L'\0') {
    sm::log::Write(sm::LogLevel::Warn, "fonts", "取系统字体目录失败，将使用内置字体");
    return std::string();
  }
  return util::narrow(buffer);
}

std::string ResolveFontFile(const std::string& font_dir) {
  if (font_dir.empty()) return std::string();
  for (const char* name : kFallbackChain) {
    const std::string candidate = util::path_join(font_dir, name);
    if (util::file_exists(candidate)) return candidate;
  }
  return std::string();
}

std::string InstallInto(ImFontAtlas* atlas, float size_px, ImFont** out_font) {
  if (atlas == nullptr) return std::string();
  if (out_font != nullptr) *out_font = nullptr;

  ImFontConfig config;
  config.OversampleH = 1;
  config.OversampleV = 1;
  config.FontDataOwnedByAtlas = true;
  config.Flags |= ImFontFlags_NoLoadError;

  const std::string dir = SystemFontDir();
  const std::string file = ResolveFontFile(dir);

  if (!file.empty()) {
    ImFont* const font =
        atlas->AddFontFromFileTTF(file.c_str(), size_px, &config, GlyphRanges(atlas));
    if (font != nullptr) {
      if (out_font != nullptr) *out_font = font;
      return file;
    }
    sm::log::Writef(sm::LogLevel::Error, "fonts", "字体文件无法载入：%s",
                    file.c_str());
  } else {
    sm::log::Writef(sm::LogLevel::Error, "fonts",
                    "回退链上的字体一个都没有（在 %s 下找过 msyh.ttc / simhei.ttf / "
                    "simsun.ttc）",
                    dir.empty() ? "?" : dir.c_str());
  }

  ImFont* const fallback = atlas->AddFontDefault(&config);
  if (out_font != nullptr) *out_font = fallback;
  sm::log::Write(sm::LogLevel::Error, "fonts",
                 "已退回内置字体：界面上的中文将显示为问号");
  return std::string();
}

}  // namespace ui_fonts
}  // namespace host
}  // namespace sm

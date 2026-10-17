// SimpleMessage — 中文字体的实现（字体目录 + imgui 图集装载）
#include "host/ui_fonts.h"

#include "imgui.h"

#include "host/log.h"
#include "host/util.h"
#include "platform/system.h"

namespace sm {
namespace host {
namespace ui_fonts {
namespace {

const ImWchar* GlyphRanges(ImFontAtlas* atlas) {
  return atlas->GetGlyphRangesChineseSimplifiedCommon();
}

}  // namespace

std::string SystemFontDir() {
  std::string dir = platform::system_font_directory();
  if (dir.empty()) {
    sm::log::Write(sm::LogLevel::Warn, "fonts", "取系统字体目录失败，将使用内置字体");
  }
  return dir;
}

std::string ResolveFontFile(const std::string& font_dir) {
  if (font_dir.empty()) return std::string();
  for (const std::string& name : platform::preferred_font_files()) {
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
    std::string names;
    for (const std::string& name : platform::preferred_font_files()) {
      if (!names.empty()) names += " / ";
      names += name;
    }
    sm::log::Writef(sm::LogLevel::Error, "fonts",
                    "回退链上的字体一个都没有（在 %s 下找过 %s）",
                    dir.empty() ? "?" : dir.c_str(), names.c_str());
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

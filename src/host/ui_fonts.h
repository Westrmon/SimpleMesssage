// SimpleMessage — 中文字体：回退链、字符集与图集装载
#pragma once

#include <string>

struct ImFont;
struct ImFontAtlas;

namespace sm {
namespace host {
namespace ui_fonts {

inline constexpr const char* kFallbackChain[] = {"msyh.ttc", "simhei.ttf",
                                                 "simsun.ttc"};

std::string SystemFontDir();

std::string ResolveFontFile(const std::string& font_dir);

std::string InstallInto(ImFontAtlas* atlas, float size_px, ImFont** out_font);

}  // namespace ui_fonts
}  // namespace host
}  // namespace sm

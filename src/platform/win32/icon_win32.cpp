// SimpleMessage — 应用图标（Win32 实现）
#include "platform/win32/icon.h"

#include <cstring>
#include <new>

#include "platform/win32/app_icon_id.h"

#include "host/log.h"

namespace sm {
namespace platform {
namespace win32 {
namespace {

constexpr int kMaxCachedIcons = 16;

struct CachedIcon {
  int        size = 0;
  bool       has_level = false;
  sm::Level  level = sm::Level::Info;
  HICON      icon = nullptr;
};

CachedIcon g_cache[kMaxCachedIcons];
int        g_cache_count = 0;

HICON FindCached(int size_px, bool has_level, sm::Level level) {
  for (int i = 0; i < g_cache_count; ++i) {
    const CachedIcon& entry = g_cache[i];
    if (entry.size == size_px && entry.has_level == has_level &&
        (!has_level || entry.level == level)) {
      return entry.icon;
    }
  }
  return nullptr;
}

void Remember(int size_px, bool has_level, sm::Level level, HICON icon) {
  if (icon == nullptr || g_cache_count >= kMaxCachedIcons) return;
  CachedIcon& entry = g_cache[g_cache_count++];
  entry.size = size_px;
  entry.has_level = has_level;
  entry.level = level;
  entry.icon = icon;
}

COLORREF BadgeColor(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:
    case sm::Level::Info:
      return RGB(0x2F, 0x80, 0xED);
    case sm::Level::Warning:
      return RGB(0xE8, 0xA1, 0x0C);
    case sm::Level::Error:
      return RGB(0xE0, 0x3A, 0x3A);
    case sm::Level::Critical:
      return RGB(0x8B, 0x0F, 0x1F);
  }
  return RGB(0x2F, 0x80, 0xED);
}

HICON ComposeLevelIcon(HICON base, COLORREF badge, int size_px) {
  ICONINFO info{};
  if (!::GetIconInfo(base, &info)) return base;

  HDC screen = ::GetDC(nullptr);
  HDC memory = (screen != nullptr) ? ::CreateCompatibleDC(screen) : nullptr;
  if (screen == nullptr || memory == nullptr || info.hbmColor == nullptr) {
    if (memory != nullptr) ::DeleteDC(memory);
    if (screen != nullptr) ::ReleaseDC(nullptr, screen);
    if (info.hbmColor != nullptr) ::DeleteObject(info.hbmColor);
    if (info.hbmMask != nullptr) ::DeleteObject(info.hbmMask);
    return base;
  }

  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(header);
  header.bV5Width = size_px;
  header.bV5Height = -size_px;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000u;
  header.bV5GreenMask = 0x0000FF00u;
  header.bV5BlueMask = 0x000000FFu;
  header.bV5AlphaMask = 0xFF000000u;

  void* raw = nullptr;
  HBITMAP target = ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header),
                                      DIB_RGB_COLORS, &raw, nullptr, 0);
  HICON result = base;
  if (target != nullptr && raw != nullptr &&
      ::GetDIBits(memory, info.hbmColor, 0, static_cast<UINT>(size_px), raw,
                  reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS) != 0) {
    auto* pixels = static_cast<unsigned char*>(raw);

    bool has_alpha = false;
    for (int i = 3; i < size_px * size_px * 4; i += 4) {
      if (pixels[i] != 0) {
        has_alpha = true;
        break;
      }
    }

    if (!has_alpha) {
      sm::log::Writef(sm::LogLevel::Warn, "icon",
                      "%dpx 的图标不带 alpha 通道，未加级别角标", size_px);
    } else {
      const int radius = size_px / 4;
      const int center = size_px - radius - 1;
      for (int y = center - radius; y <= center + radius; ++y) {
        for (int x = center - radius; x <= center + radius; ++x) {
          if (x < 0 || y < 0 || x >= size_px || y >= size_px) continue;
          const int dx = x - center;
          const int dy = y - center;
          if (dx * dx + dy * dy > radius * radius) continue;
          unsigned char* pixel = pixels + (y * size_px + x) * 4;
          pixel[0] = GetBValue(badge);
          pixel[1] = GetGValue(badge);
          pixel[2] = GetRValue(badge);
          pixel[3] = 255;
        }
      }

      const HBITMAP mask = ::CreateBitmap(size_px, size_px, 1, 1, nullptr);
      if (mask != nullptr) {
        ICONINFO composed{};
        composed.fIcon = TRUE;
        composed.hbmColor = target;
        composed.hbmMask = mask;
        const HICON icon = ::CreateIconIndirect(&composed);
        if (icon != nullptr) result = icon;
        ::DeleteObject(mask);
      }
    }
  }

  if (target != nullptr) ::DeleteObject(target);
  ::DeleteDC(memory);
  ::ReleaseDC(nullptr, screen);
  ::DeleteObject(info.hbmColor);
  if (info.hbmMask != nullptr) ::DeleteObject(info.hbmMask);
  return result;
}

}  // namespace

HICON LoadAppIcon(int size_px) {
  if (size_px <= 0) size_px = ::GetSystemMetrics(SM_CXSMICON);

  if (HICON cached = FindCached(size_px, false, sm::Level::Info)) return cached;

  HICON icon = static_cast<HICON>(::LoadImageW(
      ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(SM_APP_ICON_ID), IMAGE_ICON,
      size_px, size_px, LR_DEFAULTCOLOR));
  if (icon == nullptr) {
    sm::log::Writef(sm::LogLevel::Warn, "icon", "取应用图标失败（%dpx），改用系统图标",
                    size_px);
    icon = ::LoadIconW(nullptr, IDI_APPLICATION);
  }
  Remember(size_px, false, sm::Level::Info, icon);
  return icon;
}

HICON LoadLevelIcon(const sm::Level* level, int size_px) {
  if (level == nullptr) return LoadAppIcon(size_px);
  if (size_px <= 0) size_px = ::GetSystemMetrics(SM_CXSMICON);

  if (HICON cached = FindCached(size_px, true, *level)) return cached;

  const HICON base = LoadAppIcon(size_px);
  if (base == nullptr) return nullptr;
  const HICON icon = ComposeLevelIcon(base, BadgeColor(*level), size_px);
  Remember(size_px, true, *level, icon);
  return icon;
}

}  // namespace win32
}  // namespace platform
}  // namespace sm

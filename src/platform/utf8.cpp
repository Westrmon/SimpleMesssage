// SimpleMessage — UTF-8 与宽字符互转（与操作系统无关）
#include "platform/utf8.h"

#include <cstddef>
#include <cstdint>

#include "sm/summary.h"

namespace sm {
namespace platform {
namespace utf8 {
namespace {

std::size_t decode_utf8(const char* s, std::uint32_t* out_codepoint) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  const std::uint32_t lead = p[0];
  std::size_t len = sm::utf8_sequence_length(p[0]);
  for (std::size_t k = 1; k < len; ++k) {
    if ((p[k] & 0xC0u) != 0x80u) {
      len = k;
      break;
    }
  }

  std::uint32_t cp = lead;
  if (len == 2) {
    cp = ((lead & 0x1Fu) << 6) | (p[1] & 0x3Fu);
  } else if (len == 3) {
    cp = ((lead & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
  } else if (len == 4) {
    cp = ((lead & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
         ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
  }
  *out_codepoint = cp;
  return len;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

}  // namespace

std::string narrow(const wchar_t* w) {
  std::string out;
  if (w == nullptr) return out;

  for (std::size_t i = 0; w[i] != 0; ++i) {
    std::uint32_t cp = static_cast<std::uint32_t>(w[i]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0xD800u && cp <= 0xDBFFu) {
        const std::uint32_t lo = static_cast<std::uint32_t>(w[i + 1]);
        if (lo >= 0xDC00u && lo <= 0xDFFFu) {
          cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
          ++i;
        }
      }
    }
    append_utf8(out, cp);
  }
  return out;
}

std::wstring widen(const char* utf8_text) {
  std::wstring out;
  if (utf8_text == nullptr) return out;

  for (std::size_t i = 0; utf8_text[i] != '\0';) {
    std::uint32_t cp = 0;
    i += decode_utf8(utf8_text + i, &cp);

    if constexpr (sizeof(wchar_t) >= 4) {
      out.push_back(static_cast<wchar_t>(cp));
    } else {
      if (cp <= 0xFFFFu) {
        out.push_back(static_cast<wchar_t>(cp));
      } else {
        const std::uint32_t v = cp - 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
      }
    }
  }
  return out;
}

}  // namespace utf8
}  // namespace platform
}  // namespace sm

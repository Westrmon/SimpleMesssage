// SimpleMessage SDK — 简略文本生成与 UTF-8 工具
#pragma once

#include <cstddef>
#include <cstdint>

namespace sm {

// UTF-8 工具

// 返回 lead 字节开头的 UTF-8 序列长度（1..4）。
inline std::size_t utf8_sequence_length(unsigned char lead) {
  if (lead < 0x80u) return 1;
  if ((lead & 0xE0u) == 0xC0u) return 2;
  if ((lead & 0xF0u) == 0xE0u) return 3;
  if ((lead & 0xF8u) == 0xF0u) return 4;
  return 1;
}

// 统计 UTF-8 字符串的码点个数（不含结尾 NUL）。
inline std::size_t utf8_codepoint_count(const char* s) {
  if (s == nullptr) return 0;
  std::size_t n = 0;
  std::size_t i = 0;
  while (s[i] != '\0') {
    const std::size_t len = utf8_sequence_length(static_cast<unsigned char>(s[i]));
    for (std::size_t k = 1; k < len; ++k) {
      if (s[i + k] == '\0') return n + 1;
    }
    i += len;
    ++n;
  }
  return n;
}

// 把 i 前进到下一个码点边界。
inline std::size_t utf8_next(const char* s, std::size_t i) {
  if (s == nullptr || s[i] == '\0') return i;
  const std::size_t len = utf8_sequence_length(static_cast<unsigned char>(s[i]));
  std::size_t j = i + 1;
  while (j < i + len && s[j] != '\0') ++j;
  return j;
}

namespace detail {

inline bool sm_is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}  // namespace detail

// make_summary — 把 src 折叠空白后取最多 max_codepoints 个码点写入 dst，
// 被截断时追加省略号；返回写入的字节数（不含结尾 NUL）。
// 完整规则与示例见 docs/插件开发/02-SDK接口参考.md §10.2。
inline std::size_t make_summary(const char* src, std::size_t max_codepoints,
                                char* dst, std::size_t dst_capacity) {
  if (dst == nullptr || dst_capacity == 0) return 0;
  dst[0] = '\0';
  if (src == nullptr || max_codepoints == 0) return 0;

  const unsigned char* p = reinterpret_cast<const unsigned char*>(src);
  std::size_t i = 0;
  std::size_t out = 0;
  std::size_t used = 0;
  bool pending_space = false;
  bool truncated = false;

  while (p[i] != 0) {
    if (detail::sm_is_ascii_space(static_cast<char>(p[i]))) {
      if (used > 0) pending_space = true;
      ++i;
      continue;
    }

    if (used >= max_codepoints) {
      truncated = true;
      break;
    }

    std::size_t len = utf8_sequence_length(p[i]);
    for (std::size_t k = 1; k < len; ++k) {
      if (p[i + k] == 0) { len = k; break; }
    }

    // 需要写入的字节数 = 折叠空格(0 或 1) + 该码点字节数
    const std::size_t need = (pending_space ? 1u : 0u) + len;
    if (out + need >= dst_capacity) {
      truncated = true;
      break;
    }

    if (pending_space) {
      dst[out++] = ' ';
      pending_space = false;
    }
    for (std::size_t k = 0; k < len; ++k) dst[out++] = static_cast<char>(p[i + k]);
    i += len;
    ++used;
  }

  // 若是因为配额/容量停下的，检查后面是否真的还有内容（避免给"刚好 20 字"的文本加一个多余的省略号），同时也处理"末尾冒出一个被折叠的空白"的情况。
  if (truncated) {
    bool more = false;
    for (std::size_t j = i; p[j] != 0; ++j) {
      if (!detail::sm_is_ascii_space(static_cast<char>(p[j]))) { more = true; break; }
    }
    if (!more) {
      // 后面只剩空白（例如 "……单词 "），此时不该加省略号。
      truncated = false;
    }
  }

  if (truncated && out + 3 < dst_capacity) {
    dst[out++] = static_cast<char>(0xE2);
    dst[out++] = static_cast<char>(0x80);
    dst[out++] = static_cast<char>(0xA6);
  }

  dst[out] = '\0';
  return out;
}

}  // namespace sm

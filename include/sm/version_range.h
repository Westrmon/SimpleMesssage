// SimpleMessage SDK — 版本号与版本范围
#pragma once

#include <cstddef>
#include <cstdint>

namespace sm {

// Version
struct Version {
  std::int32_t major = 0;
  std::int32_t minor = 0;
  std::int32_t patch = 0;
};

namespace detail {

inline bool vr_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool vr_is_digit(char c) { return c >= '0' && c <= '9'; }

// 跳过 s[i] 处的空白，返回新的下标。
inline std::size_t vr_skip_space(const char* s, std::size_t i) {
  while (s[i] != '\0' && vr_is_space(s[i])) ++i;
  return i;
}

// 从 s[i] 起解析一个非负整数，写入 out；返回解析结束后的下标。
inline std::size_t vr_parse_int(const char* s, std::size_t i, std::int64_t* out) {
  if (!vr_is_digit(s[i])) return static_cast<std::size_t>(-1);
  std::int64_t v = 0;
  while (vr_is_digit(s[i])) {
    v = v * 10 + (s[i] - '0');
    if (v > 1000000) v = 1000000;
    ++i;
  }
  *out = v;
  return i;
}

}  // namespace detail

// 解析版本号字符串。
inline bool parse_version(const char* text, Version* out) {
  if (text == nullptr || out == nullptr) return false;

  std::size_t i = detail::vr_skip_space(text, 0);
  if (text[i] == '\0') return false;

  std::int64_t parts[3] = {0, 0, 0};
  int count = 0;
  for (;;) {
    if (count >= 3) return false;
    std::size_t next = detail::vr_parse_int(text, i, &parts[count]);
    if (next == static_cast<std::size_t>(-1)) return false;
    ++count;
    i = next;
    if (text[i] == '.') {
      ++i;
      continue;
    }
    break;
  }

  i = detail::vr_skip_space(text, i);
  if (text[i] != '\0') return false;

  out->major = static_cast<std::int32_t>(parts[0]);
  out->minor = static_cast<std::int32_t>(parts[1]);
  out->patch = static_cast<std::int32_t>(parts[2]);
  return true;
}

// 三路比较：a < b 返回 -1，相等 0，a > b 返回 1。
inline int compare_version(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

namespace detail {

// 解析一个"比较项"（如 ">=1.2.3"），失败返回 (std::size_t)-1。
inline std::size_t vr_parse_term(const char* s, std::size_t i, int* op, Version* ver) {
  i = vr_skip_space(s, i);
  int o = '=';
  if (s[i] == '>') {
    ++i;
    if (s[i] == '=') { ++i; o = 'G'; } else { o = '>'; }
  } else if (s[i] == '<') {
    ++i;
    if (s[i] == '=') { ++i; o = 'L'; } else { o = '<'; }
  } else if (s[i] == '=') {
    ++i;
    o = '=';
  }
  i = vr_skip_space(s, i);

  // 截取到下一个 ',' 或 '|' 或结尾
  char buf[64];
  std::size_t n = 0;
  while (s[i] != '\0' && s[i] != ',' && s[i] != '|' && !vr_is_space(s[i])) {
    if (n + 1 >= sizeof(buf)) return static_cast<std::size_t>(-1);
    buf[n++] = s[i++];
  }
  buf[n] = '\0';
  if (!parse_version(buf, ver)) return static_cast<std::size_t>(-1);

  *op = o;
  return i;
}

inline bool vr_term_matches(int op, const Version& lhs, const Version& rhs) {
  const int c = compare_version(lhs, rhs);
  switch (op) {
    case '=': return c == 0;
    case '>': return c > 0;
    case 'G': return c >= 0;
    case '<': return c < 0;
    case 'L': return c <= 0;
    default: return false;
  }
}

// 校验整段范围表达式是否合法。
inline bool vr_validate(const char* range) {
  if (range == nullptr) return false;
  std::size_t i = vr_skip_space(range, 0);
  if (range[i] == '\0') return true;

  bool any_term_in_group = false;
  for (;;) {
    i = vr_skip_space(range, i);
    if (range[i] == '\0') break;
    if (range[i] == ',' || range[i] == '|') return false;

    int op = '=';
    Version v{};
    i = vr_parse_term(range, i, &op, &v);
    if (i == static_cast<std::size_t>(-1)) return false;
    any_term_in_group = true;

    i = vr_skip_space(range, i);
    if (range[i] == ',') {
      ++i;
      continue;
    }
    if (range[i] == '|') {
      ++i;
      any_term_in_group = false;
      continue;
    }
    if (range[i] == '\0') break;
    return false;
  }
  (void)any_term_in_group;
  return true;
}

}  // namespace detail

// 判断范围表达式是否合法。
inline bool is_valid_range(const char* range) { return detail::vr_validate(range); }

// 判断一个具体版本是否满足范围。
inline bool version_satisfies(const char* version, const char* range, const char** error_out = nullptr) {
  if (error_out != nullptr) *error_out = nullptr;

  Version v{};
  if (!parse_version(version, &v)) {
    if (error_out != nullptr) *error_out = "版本号格式非法";
    return false;
  }
  if (range == nullptr) {
    if (error_out != nullptr) *error_out = "范围表达式为空指针";
    return false;
  }
  if (!detail::vr_validate(range)) {
    if (error_out != nullptr) *error_out = "范围表达式语法非法";
    return false;
  }

  std::size_t i = detail::vr_skip_space(range, 0);
  if (range[i] == '\0') return true;

  // 按 '|' 分组，任一组全部满足即通过。
  for (;;) {
    bool group_ok = true;
    bool group_has_term = false;
    for (;;) {
      i = detail::vr_skip_space(range, i);
      if (range[i] == '\0' || range[i] == '|') break;

      int op = '=';
      Version rhs{};
      i = detail::vr_parse_term(range, i, &op, &rhs);
      if (i == static_cast<std::size_t>(-1)) {
        if (error_out != nullptr) *error_out = "范围表达式语法非法";
        return false;
      }
      group_has_term = true;
      if (!detail::vr_term_matches(op, v, rhs)) group_ok = false;

      i = detail::vr_skip_space(range, i);
      if (range[i] == ',') { ++i; continue; }
      break;
    }
    if (!group_has_term) return true;
    if (group_ok) return true;
    if (range[i] == '|') { ++i; continue; }
    break;
  }

  if (error_out != nullptr) *error_out = "宿主版本不在插件声明的范围内";
  return false;
}

}  // namespace sm

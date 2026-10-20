// SimpleMessage — 通用 {name} 占位符展开与路径清洗的实现
#include "host/placeholders.h"

#include <array>
#include <cstddef>
#include <cstdio>

namespace sm {
namespace host {
namespace {

constexpr std::size_t kMaxPathValueBytes = 120;

bool IsNameStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_';
}

bool IsValidName(std::string_view name) {
  if (name.empty()) return false;
  for (const char c : name) {
    if (!IsNameStart(c)) return false;
  }
  return true;
}

bool IsContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

void TruncateUtf8(std::string* text, std::size_t max_bytes) {
  if (text->size() <= max_bytes) return;
  std::size_t cut = max_bytes;
  while (cut > 0 && IsContinuationByte((*text)[cut])) --cut;
  text->resize(cut);
}

void TrimTail(std::string* text) {
  while (!text->empty() && (text->back() == ' ' || text->back() == '.')) {
    text->pop_back();
  }
}

bool IsReservedDeviceName(const std::string& text) {
  const std::size_t dot = text.find('.');
  std::string base = (dot == std::string::npos) ? text : text.substr(0, dot);
  for (char& c : base) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  static const std::array<const char*, 22> kReserved = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5",
      "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
      "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* name : kReserved) {
    if (base == name) return true;
  }
  return false;
}

void RecordUnknown(std::vector<std::string>* out, const std::string& name) {
  if (out == nullptr) return;
  for (const std::string& existing : *out) {
    if (existing == name) return;
  }
  out->push_back(name);
}

}  // namespace

std::string ExpandPlaceholders(std::string_view input, PlaceholderMode mode,
                               const PlaceholderResolver& resolver,
                               std::vector<std::string>* out_unknown) {
  std::string out;
  out.reserve(input.size());

  std::size_t i = 0;
  while (i < input.size()) {
    const char c = input[i];
    if (c == '{') {
      if (i + 1 < input.size() && input[i + 1] == '{') {
        out.push_back('{');
        i += 2;
        continue;
      }
      const std::size_t close = input.find('}', i + 1);
      if (close != std::string_view::npos) {
        const std::string_view name = input.substr(i + 1, close - i - 1);
        if (IsValidName(name)) {
          std::string value;
          if (resolver && resolver(name, &value)) {
            if (mode == PlaceholderMode::Path) {
              out += SanitizePathValue(value);
            } else {
              out += value;
            }
          } else {
            RecordUnknown(out_unknown, std::string(name));
            out += input.substr(i, close - i + 1);
          }
          i = close + 1;
          continue;
        }
      }
      out.push_back('{');
      ++i;
      continue;
    }

    if (c == '}') {
      if (i + 1 < input.size() && input[i + 1] == '}') {
        out.push_back('}');
        i += 2;
        continue;
      }
      out.push_back('}');
      ++i;
      continue;
    }

    out.push_back(c);
    ++i;
  }

  return out;
}

std::vector<std::string> FindPlaceholderNames(std::string_view input) {
  std::vector<std::string> names;
  std::size_t i = 0;
  while (i < input.size()) {
    if (input[i] == '{') {
      if (i + 1 < input.size() && input[i + 1] == '{') {
        i += 2;
        continue;
      }
      const std::size_t close = input.find('}', i + 1);
      if (close != std::string_view::npos) {
        const std::string_view name = input.substr(i + 1, close - i - 1);
        if (IsValidName(name)) {
          std::string text(name);
          bool seen = false;
          for (const std::string& existing : names) {
            if (existing == text) {
              seen = true;
              break;
            }
          }
          if (!seen) names.push_back(std::move(text));
          i = close + 1;
          continue;
        }
      }
      ++i;
      continue;
    }
    if (input[i] == '}' && i + 1 < input.size() && input[i + 1] == '}') {
      i += 2;
      continue;
    }
    ++i;
  }
  return names;
}

bool IsKnownNotificationPlaceholder(std::string_view name) {
  for (std::string_view known : kNotificationPlaceholders) {
    if (name == known) return true;
  }
  return false;
}

std::string SanitizePathValue(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    const unsigned char u = static_cast<unsigned char>(c);
    const bool illegal = u < 0x20 || u == 0x7F || c == '<' || c == '>' || c == ':' ||
                         c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' ||
                         c == '*';
    out.push_back(illegal ? '_' : c);
  }

  TrimTail(&out);
  if (out.empty() || out == "." || out == "..") out = "_";

  TruncateUtf8(&out, kMaxPathValueBytes);
  TrimTail(&out);
  if (out.empty() || out == "." || out == "..") out = "_";

  if (IsReservedDeviceName(out)) out.insert(out.begin(), '_');
  return out;
}

}  // namespace host
}  // namespace sm

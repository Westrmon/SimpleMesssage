// SimpleMessage 示例插件 —— source.gotify

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <curl/curl.h>
#pragma warning(pop)

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"

namespace {

using sm::LogLevel;
using sm::PluginBase;
using sm::PluginKind;
using sm::PluginMeta;
using sm::SettingDef;
using sm::StartOutput;
using sm::StartResult;

constexpr std::size_t kTitleBytes = 200;
constexpr std::size_t kMaxContentBytes = 60 * 1024;

constexpr std::size_t kMaxMessageBytes = 1024 * 1024;

constexpr std::int64_t kReconnectMinSec = 3;
constexpr std::int64_t kReconnectMaxSec = 60;
constexpr std::int64_t kReadPollMs = 100;

constexpr SettingDef kSettings[] = {
    sm::setting::text("server_url", "http://127.0.0.1:8080", 512, "Gotify 服务地址",
                      "带协议的根地址，如 https://gotify.example.com 或 "
                      "http://192.168.1.5:8080。也接受 ws:// / wss://。"
                      "http 会走 ws，https 会走 wss。反向代理把 Gotify 挂在"
                      "子路径下时要把前缀一起填上，例如 "
                      "http://192.168.1.7:9000/gotify。不要带 /stream 后缀"
                      "（如果带了也不会重复拼接）。"),
    sm::setting::text("username", "", 256, "登录用户名（可选）",
                      "Gotify 网页登录用的用户名。与 password 一起填写后，插件会"
                      "通过 HTTP Basic Auth 自动创建或复用客户端令牌，不必手工到"
                      "Clients 页复制。client_token 非空时优先使用 client_token。"),
    sm::setting::text("password", "", 256, "登录密码（可选）",
                      "Gotify 网页登录用的密码，仅用于登录获取客户端令牌。"
                      "注意：当前 SDK 没有密码类型，密码会以明文随插件设置保存；"
                      "用 http:// 连接时密码在网络上只做了 Base64，建议改用 HTTPS。"),
    sm::setting::text("client_token", "", 256, "客户端令牌（可选）",
                      "Gotify 的【客户端令牌 Client Token】，不是应用令牌。"
                      "填写后直接使用它，不再登录。留空时必须填写用户名与密码。"),
    sm::setting::text("client_name", "SimpleMessage", 128, "自动创建的客户端名",
                      "登录模式下使用：插件会创建这个客户端，并把拿到的令牌缓存到"
                      "数据目录；后续启动直接复用。多个实例请使用不同的名字。"),
    sm::setting::text("stream_path", "/stream", 128, "流路径",
                      "Gotify 的 WebSocket 端点。默认 /stream。"
                      "反向代理把 Gotify 挂在子路径下时（如 /gotify），"
                      "server_url 里已经包含该前缀，这里保持默认即可。"),
    sm::setting::text("topic", "gotify.in", 128, "通知主题",
                      "通知的 topic，用于路由。只允许小写字母、数字、下划线、"
                      "点、减号，其它字符会被替换成下划线并记 Warn。"),
    sm::setting::integer("poll_interval_sec", 2, 1, 60, "投递间隔（秒）",
                         "收到的消息最多攒这么久才变成通知。通知只能在 start() "
                         "里产生，所以这个值就是从收到到看见的延迟上限。"),
    sm::setting::integer("min_priority", -128, -128, 1024, "最低优先级",
                         "低于此值的消息直接丢弃。-128 表示全部接收。"
                         "Gotify 的默认优先级是 0。"),
    sm::setting::boolean("use_priority_level", true, "用优先级决定通知级别",
                         "打开（默认）：优先级 ≥8 → Critical，≥5 → Error，"
                         "≥4 → Warning，≥0 → Info，<0 → Debug。"
                         "关闭：所有消息都记为 Info。"),
    sm::setting::integer("content_max_kb", 32, 1, 48, "正文上限（KB）",
                         "超长正文会被截断到这个大小（UTF-8 码点边界）。"
                         "上限 48 是为了低于宿主的 64 KB。"),
    sm::setting::integer("queue_max", 256, 4, 4096, "待投递队列上限",
                         "队列满时丢弃【最旧】的消息并计数（丢最新的会让用户"
                         "永远看不到当前发生的事）。"),
    sm::setting::integer("ping_interval_sec", 30, 0, 3600, "保活间隔（秒）",
                         "定期发送 WebSocket PING，避免被中间代理当成空闲连接"
                         "断开。0 表示不主动发（服务端自己的 PING 仍会被自动回应）。"),
    sm::setting::boolean("tls_verify", true, "校验服务器证书",
                         "【只在调试时才考虑关闭】关掉它等于放弃对中间人攻击的"
                         "防护。自签名证书请改用系统信任链，而不是关这个开关。"),
    sm::setting::boolean("include_extras", false, "把 extras 并入正文",
                         "打开后通知的 content 会在正文后面附上 Gotify 原始的 "
                         "extras JSON，便于排查发送方到底带了什么。"),
};

constexpr PluginMeta kMeta = {
    "source.gotify",
    "Gotify 接收",
    "SimpleMessage 示例",
    "1.1.0",
    "连接 Gotify 服务端的 WebSocket 流，把推送的消息变成通知。",
    PluginKind::Source,
    ">=0.1.0,<1.0.0",
    sm::Span<const char*>{},
    sm::span_of(kSettings),
    SM_API_VERSION,
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

void ClampUtf8(std::string* s, std::size_t max_bytes) {
  if (s->size() <= max_bytes) return;
  std::size_t i = max_bytes;
  while (i > 0 && (static_cast<unsigned char>((*s)[i]) & 0xC0) == 0x80) --i;
  s->resize(i);
}

std::string SanitizeTopic(const std::string& raw, bool* changed) {
  std::string out = ToLower(raw);
  *changed = false;
  if (out.empty()) {
    out = "gotify.in";
    *changed = true;
    return out;
  }
  for (char& c : out) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-';
    if (!ok) {
      c = '_';
      *changed = true;
    }
  }
  return out;
}

std::string UrlEncode(const std::string& s) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                            c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

bool ScanJsonString(const std::string& s, std::size_t* i, std::string* out) {
  if (*i >= s.size() || s[*i] != '"') return false;
  ++*i;
  out->clear();
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++*i;
      return true;
    }
    if (c == '\\') {
      if (*i + 1 >= s.size()) return false;
      const char e = s[*i + 1];
      switch (e) {
        case 'n': out->push_back('\n'); break;
        case 't': out->push_back('\t'); break;
        case 'r': out->push_back('\r'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case '/': out->push_back('/'); break;
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case 'u': {
          if (*i + 5 >= s.size()) return false;
          unsigned code = 0;
          for (int k = 0; k < 4; ++k) {
            const char h = s[*i + 2 + static_cast<std::size_t>(k)];
            unsigned v = 0;
            if (h >= '0' && h <= '9') v = static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') v = static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v = static_cast<unsigned>(h - 'A' + 10);
            else return false;
            code = code * 16 + v;
          }
          *i += 6;
          if (code < 0x80) {
            out->push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          continue;
        }
        default: return false;
      }
      *i += 2;
      continue;
    }
    out->push_back(c);
    ++*i;
  }
  return false;
}

bool SkipJsonValue(const std::string& s, std::size_t* i) {
  while (*i < s.size() && IsSpace(s[*i])) ++*i;
  if (*i >= s.size()) return false;
  const char c = s[*i];
  if (c == '"') {
    std::string ignored;
    return ScanJsonString(s, i, &ignored);
  }
  if (c == '{' || c == '[') {
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (*i < s.size()) {
      const char d = s[*i];
      if (d == '"') {
        std::string ignored;
        if (!ScanJsonString(s, i, &ignored)) return false;
        continue;
      }
      if (d == '{' || d == '[') ++depth;
      if (d == '}' || d == ']') {
        --depth;
        if (depth == 0) {
          ++*i;
          return d == close;
        }
      }
      ++*i;
    }
    return false;
  }
  const std::size_t start = *i;
  while (*i < s.size() && s[*i] != ',' && s[*i] != '}' && s[*i] != ']') ++*i;
  return *i > start;
}

// 在顶层对象里找 key，返回它的值区间 [value_start, value_end) 与首字符。
bool FindTopLevelValue(const std::string& body, const char* key, char* type,
                       std::size_t* value_start, std::size_t* value_end) {
  std::size_t i = 0;
  while (i < body.size() && IsSpace(body[i])) ++i;
  if (i >= body.size() || body[i] != '{') return false;
  ++i;

  const std::size_t key_len = std::strlen(key);
  while (i < body.size()) {
    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i < body.size() && body[i] == '}') return false;

    std::string name;
    if (!ScanJsonString(body, &i, &name)) return false;
    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i >= body.size() || body[i] != ':') return false;
    ++i;
    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i >= body.size()) return false;

    const std::size_t start = i;
    if (name.size() == key_len && name.compare(key) == 0) {
      if (!SkipJsonValue(body, &i)) return false;
      std::size_t end = i;
      while (end > start && IsSpace(body[end - 1])) --end;
      *type = body[start];
      *value_start = start;
      *value_end = end;
      return true;
    }
    if (!SkipJsonValue(body, &i)) return false;
    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i < body.size() && body[i] == ',') {
      ++i;
      continue;
    }
    return false;
  }
  return false;
}

bool JsonTopLevelString(const std::string& body, const char* key, std::string* out) {
  char type = '\0';
  std::size_t start = 0;
  std::size_t end = 0;
  if (!FindTopLevelValue(body, key, &type, &start, &end) || type != '"') return false;
  std::size_t i = start;
  return ScanJsonString(body, &i, out);
}

bool JsonTopLevelInt(const std::string& body, const char* key, std::int64_t* out) {
  char type = '\0';
  std::size_t start = 0;
  std::size_t end = 0;
  if (!FindTopLevelValue(body, key, &type, &start, &end)) return false;
  std::size_t i = start;
  bool negative = false;
  if (i < end && body[i] == '-') {
    negative = true;
    ++i;
  }
  if (i >= end) return false;
  std::int64_t value = 0;
  for (; i < end; ++i) {
    const char c = body[i];
    if (c < '0' || c > '9') return false;
    if (value > (INT64_MAX - (c - '0')) / 10) return false;
    value = value * 10 + (c - '0');
  }
  *out = negative ? -value : value;
  return true;
}

bool JsonTopLevelRaw(const std::string& body, const char* key, std::string* out) {
  char type = '\0';
  std::size_t start = 0;
  std::size_t end = 0;
  if (!FindTopLevelValue(body, key, &type, &start, &end)) return false;
  *out = body.substr(start, end - start);
  return true;
}

struct AppRecord {
  std::int64_t id = 0;
  std::string name;
  std::string image;
};

// GET /application 返回对象数组。这里只取每个对象的 id / name / image，
// 其余字段（token、description、createdAt……）用 SkipJsonValue 跳过。
bool ParseApplicationArray(const std::string& body, std::vector<AppRecord>* out) {
  std::size_t i = 0;
  while (i < body.size() && IsSpace(body[i])) ++i;
  if (i >= body.size() || body[i] != '[') return false;
  ++i;

  while (i < body.size()) {
    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i >= body.size()) return false;
    if (body[i] == ']') return true;
    if (body[i] != '{') {
      if (!SkipJsonValue(body, &i)) return false;
    } else {
      const std::size_t start = i;
      if (!SkipJsonValue(body, &i)) return false;
      const std::string object = body.substr(start, i - start);

      AppRecord app;
      if (!JsonTopLevelInt(object, "id", &app.id) ||
          !JsonTopLevelString(object, "name", &app.name)) {
        return false;
      }
      JsonTopLevelString(object, "image", &app.image);
      out->push_back(std::move(app));
    }

    while (i < body.size() && IsSpace(body[i])) ++i;
    if (i >= body.size()) return false;
    if (body[i] == ']') return true;
    if (body[i] != ',') return false;
    ++i;
  }
  return false;
}

// Gotify 应用图片是相对于服务端根地址的路径（例如 "image/image.jpeg"、
// "static/defaultapp.png"）；前端的做法是 base_url + image，这里保持一致。
std::string AppIconUrl(const std::string& http_base, const std::string& image) {
  if (image.empty()) return std::string();
  if (image.compare(0, 7, "http://") == 0 ||
      image.compare(0, 8, "https://") == 0) {
    return image;
  }
  if (image[0] == '/') return http_base + image;
  return http_base + "/" + image;
}

bool ReadFixedInt(const std::string& s, std::size_t* i, int digits, int* out) {
  if (*i + static_cast<std::size_t>(digits) > s.size()) return false;
  int value = 0;
  for (int k = 0; k < digits; ++k) {
    const char c = s[*i + static_cast<std::size_t>(k)];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  *i += static_cast<std::size_t>(digits);
  *out = value;
  return true;
}

std::int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
  y -= (m <= 2) ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = static_cast<unsigned>(
      (153 * (static_cast<int>(m) + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<int>(d) - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool ParseRfc3339Ms(const std::string& s, std::int64_t* out) {
  std::size_t i = 0;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (!ReadFixedInt(s, &i, 4, &year)) return false;
  if (i >= s.size() || s[i++] != '-') return false;
  if (!ReadFixedInt(s, &i, 2, &month)) return false;
  if (i >= s.size() || s[i++] != '-') return false;
  if (!ReadFixedInt(s, &i, 2, &day)) return false;
  if (i >= s.size() || (s[i] != 'T' && s[i] != 't' && s[i] != ' ')) return false;
  ++i;
  if (!ReadFixedInt(s, &i, 2, &hour)) return false;
  if (i >= s.size() || s[i++] != ':') return false;
  if (!ReadFixedInt(s, &i, 2, &minute)) return false;
  if (i >= s.size() || s[i++] != ':') return false;
  if (!ReadFixedInt(s, &i, 2, &second)) return false;

  int millis = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    int digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      if (digits < 3) millis = millis * 10 + (s[i] - '0');
      ++digits;
      ++i;
    }
    if (digits == 0) return false;
    while (digits < 3) {
      millis *= 10;
      ++digits;
    }
  }

  int offset_sec = 0;
  if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
    ++i;
  } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    const int sign = (s[i] == '-') ? -1 : 1;
    ++i;
    int off_hour = 0;
    int off_minute = 0;
    if (!ReadFixedInt(s, &i, 2, &off_hour)) return false;
    if (i >= s.size() || s[i++] != ':') return false;
    if (!ReadFixedInt(s, &i, 2, &off_minute)) return false;
    offset_sec = sign * (off_hour * 3600 + off_minute * 60);
  } else {
    return false;
  }
  if (i != s.size()) return false;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 60) {
    return false;
  }

  const std::int64_t days = DaysFromCivil(year, static_cast<unsigned>(month),
                                          static_cast<unsigned>(day));
  const std::int64_t seconds =
      days * 86400 + hour * 3600 + minute * 60 + second - offset_sec;
  *out = seconds * 1000 + millis;
  return true;
}

sm::Level LevelForPriority(std::int64_t priority, bool use_priority) {
  if (!use_priority) return sm::Level::Info;
  if (priority >= 8) return sm::Level::Critical;
  if (priority >= 5) return sm::Level::Error;
  if (priority >= 4) return sm::Level::Warning;
  if (priority >= 0) return sm::Level::Info;
  return sm::Level::Debug;
}

struct Pending {
  std::string title;
  std::string content;
  std::string summary;
  std::string icon;
  std::string topic;
  std::string id;
  sm::Level level = sm::Level::Info;
  std::int64_t time_ms = 0;
};

struct AppInfo {
  std::string name;
  std::string icon;  // 可直接交给 Notification::icon 的 http(s) 地址
};

// start() 读到并校验过的设置快照。接收线程只读这份快照，绝不调 setting_*。
struct Config {
  bool configured = false;
  std::uint64_t generation = 0;

  std::string http_base;    // http(s)://host[:port][/prefix]，用于 /client 登录
  std::string stream_base;  // ws(s)://host[:port][/prefix]/stream，不含 token
  std::string display_url;  // 与 stream_base 相同，仅用于日志（不含令牌）
  std::string identity;     // 决定是否需要重连的键
  std::string problem;      // 配置不可用时的原因（给用户看）

  std::string client_token;       // 手工配置的 Client Token；非空时不再登录
  std::string username;
  std::string password;
  std::string client_name;        // 登录模式下创建的客户端名
  std::string token_cache_path;   // 登录拿到的 token 的本地缓存文件

  std::string topic;
  std::int64_t poll_interval_sec = 2;
  std::int64_t min_priority = -128;
  std::int64_t ping_interval_sec = 30;
  std::size_t content_bytes = 32 * 1024;
  std::size_t queue_max = 256;
  bool use_priority_level = true;
  bool tls_verify = true;
  bool include_extras = false;
};

// 把用户填的根地址拆成两部分：
//   http_base   = http(s)://host[:port][/prefix]       —— 用于 /client 登录
//   stream_base = ws(s)://host[:port][/prefix]/stream  —— 用于 WebSocket
bool BuildEndpoints(const std::string& server_in, const std::string& path_in,
                    std::string* http_base, std::string* stream_base,
                    std::string* problem) {
  const std::string server = Trim(server_in);
  if (server.empty()) {
    *problem =
        "尚未配置 Gotify 服务地址（server_url 为空），本插件不会接收任何消息。"
        "在控制面板的插件页填上服务端根地址即可。";
    return false;
  }

  const std::size_t sep = server.find("://");
  std::string scheme;
  std::string rest;
  if (sep == std::string::npos) {
    scheme = "http";
    rest = server;
  } else {
    scheme = ToLower(server.substr(0, sep));
    rest = server.substr(sep + 3);
  }

  std::string web_scheme;
  std::string ws_scheme;
  if (scheme == "http" || scheme == "ws") {
    web_scheme = "http";
    ws_scheme = "ws";
  } else if (scheme == "https" || scheme == "wss") {
    web_scheme = "https";
    ws_scheme = "wss";
  } else {
    *problem = "server_url 的协议只支持 http / https / ws / wss，当前是 \"" + scheme + "\"。";
    return false;
  }

  while (!rest.empty() && rest.back() == '/') rest.pop_back();
  if (rest.empty()) {
    *problem = "server_url 里没有主机名。";
    return false;
  }

  std::string path = Trim(path_in);
  if (path.empty()) path = "/stream";
  if (path[0] != '/') path = "/" + path;

  // 用户如果把完整流地址（.../stream）也填进 server_url，不要拼成 /stream/stream。
  const bool already_stream =
      rest.size() >= path.size() &&
      rest.compare(rest.size() - path.size(), path.size(), path) == 0;

  std::string prefix = already_stream ? rest.substr(0, rest.size() - path.size()) : rest;
  while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();

  *http_base = web_scheme + "://" + prefix;
  *stream_base = ws_scheme + "://" + prefix + path;
  return true;
}

struct HttpResponse {
  long status = 0;
  std::string body;
};

std::size_t WriteResponseBody(char* ptr, std::size_t size, std::size_t nmemb,
                              void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string JsonEscape(const std::string& s) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(kHex[c >> 4]);
          out.push_back(kHex[c & 0x0F]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

std::string BodySnippet(const std::string& body) {
  std::string out = Trim(body);
  ClampUtf8(&out, 200);
  return out;
}

enum class HttpMethod { Get, Post, Put };

// gotify_key 非空时用 X-Gotify-Key 认证（客户端令牌），否则用 Basic Auth。
// /application 只接受令牌或 Basic Auth；这里按调用方的上下文二选一。
bool HttpRequest(const std::string& url, const Config& cfg, HttpMethod method,
                 const std::string& payload, const std::string& gotify_key,
                 HttpResponse* out, std::string* error) {
  if (gotify_key.find_first_of("\r\n") != std::string::npos) {
    *error = "客户端令牌包含非法换行符";
    return false;
  }

  CURL* easy = curl_easy_init();
  if (easy == nullptr) {
    *error = "无法创建 libcurl 会话";
    return false;
  }

  char errbuf[CURL_ERROR_SIZE];
  errbuf[0] = '\0';

  struct curl_slist* headers = nullptr;
  const std::string userpwd = cfg.username + ":" + cfg.password;

  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  if (gotify_key.empty()) {
    curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(easy, CURLOPT_USERPWD, userpwd.c_str());
  } else {
    const std::string key_header = "X-Gotify-Key: " + gotify_key;
    headers = curl_slist_append(headers, key_header.c_str());
  }
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "SimpleMessage/source.gotify");
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, cfg.tls_verify ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, cfg.tls_verify ? 2L : 0L);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteResponseBody);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &out->body);

  if (method == HttpMethod::Get) {
    curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
  } else {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(payload.size()));
    // CUSTOMREQUEST 必须放在 POSTFIELDS 之后，否则会被 POST 覆盖。
    if (method == HttpMethod::Put) {
      curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
    } else {
      curl_easy_setopt(easy, CURLOPT_POST, 1L);
    }
  }
  if (headers != nullptr) {
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
  }

  const CURLcode code = curl_easy_perform(easy);
  bool ok = true;
  if (code != CURLE_OK) {
    *error = (errbuf[0] != '\0') ? errbuf : curl_easy_strerror(code);
    ok = false;
  } else {
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &out->status);
  }

  if (headers != nullptr) curl_slist_free_all(headers);
  curl_easy_cleanup(easy);
  return ok;
}

// Gotify 3.x 的 GET /client 不返回 token（PUT 返回的 token 也不可用），
// 所以登录创建客户端后必须自己把 token 存下来，否则每次启动都会重复建客户端。
bool LoadCachedToken(const Config& cfg, std::string* token) {
  if (cfg.token_cache_path.empty()) return false;
  std::ifstream in(cfg.token_cache_path, std::ios::binary);
  if (!in) return false;

  std::string body;
  char buffer[4096];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    body.append(buffer, static_cast<std::size_t>(in.gcount()));
  }

  std::string server;
  std::string username;
  std::string client_name;
  std::string cached;
  if (!JsonTopLevelString(body, "server", &server) ||
      !JsonTopLevelString(body, "username", &username) ||
      !JsonTopLevelString(body, "client_name", &client_name) ||
      !JsonTopLevelString(body, "token", &cached)) {
    return false;
  }
  // 服务器、用户或客户端名变了就不能复用旧 token。
  if (server != cfg.http_base || username != cfg.username ||
      client_name != cfg.client_name || cached.empty()) {
    return false;
  }
  *token = cached;
  return true;
}

bool SaveCachedToken(const Config& cfg, const std::string& token) {
  if (cfg.token_cache_path.empty()) return false;
  const std::string body =
      "{\"server\":\"" + JsonEscape(cfg.http_base) + "\",\"username\":\"" +
      JsonEscape(cfg.username) + "\",\"client_name\":\"" +
      JsonEscape(cfg.client_name) + "\",\"token\":\"" + JsonEscape(token) + "\"}";
  std::ofstream out(cfg.token_cache_path,
                    std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(body.data(), static_cast<std::streamsize>(body.size()));
  return out.good();
}

void ClearCachedToken(const Config& cfg) {
  if (!cfg.token_cache_path.empty()) {
    std::remove(cfg.token_cache_path.c_str());
  }
}

bool ObtainClientToken(const Config& cfg, std::string* token, bool* from_cache,
                       std::string* error) {
  *from_cache = false;
  if (LoadCachedToken(cfg, token)) {
    *from_cache = true;
    return true;
  }

  const std::string endpoint = cfg.http_base + "/client";
  const std::string payload =
      "{\"name\":\"" + JsonEscape(cfg.client_name) + "\"}";
  HttpResponse created;
  std::string http_error;
  if (!HttpRequest(endpoint, cfg, HttpMethod::Post, payload, std::string(),
                   &created, &http_error)) {
    *error = "登录失败（" + endpoint + "）：" + http_error;
    return false;
  }
  if (created.status == 401 || created.status == 403) {
    *error = "登录失败：用户名或密码不正确（HTTP " +
             std::to_string(created.status) + "）";
    return false;
  }
  if ((created.status == 200 || created.status == 201) &&
      JsonTopLevelString(created.body, "token", token) && !token->empty()) {
    // 缓存写不进去不影响本次连接，只是下次启动会重新创建客户端。
    (void)SaveCachedToken(cfg, *token);
    return true;
  }
  *error = "创建客户端失败（HTTP " + std::to_string(created.status) + "）：" +
           BodySnippet(created.body);
  return false;
}

class GotifySource final : public PluginBase {
 public:
  explicit GotifySource(sm::IHostContext* host) : PluginBase(host, kMeta) {}
  ~GotifySource() override { StopListener(); }

  bool initialize() override;
  StartResult start(StartOutput* out) override;
  void stop() override;
  void exit() override;

 private:
  Config ReadConfig();
  bool EnsureThread();

  void ListenLoop();
  bool OpenConnection(const std::string& stream_url, const Config& cfg, CURL** easy,
                      long* http_status, std::string* error);
  enum class ReadStatus { Data, Again, Error };
  ReadStatus ReadOnce(CURL* easy, const Config& cfg, std::string* frame,
                      bool* oversize, std::string* error);
  void HandleMessage(const std::string& json, const Config& cfg);
  bool FetchApplications(const Config& cfg, const std::string& token,
                         std::string* error);
  void SendPing(CURL* easy);

  void StopListener();
  void SleepInterruptible(std::int64_t ms);
  Config SnapshotConfig();
  void SetLastError(const std::string& error);

  // 调度线程与接收线程之间唯一的共享状态。
  std::mutex mutex_;
  Config config_;
  std::deque<Pending> queue_;
  std::uint64_t dropped_ = 0;
  std::uint64_t rejected_ = 0;
  std::uint64_t parse_failed_ = 0;
  std::string last_error_;
  std::uint64_t config_generation_ = 0;
  std::string current_identity_;
  bool current_configured_ = false;

  // 调度线程私有。
  std::string reported_error_;
  std::string reported_problem_;
  bool thread_started_ = false;

  // 线程生命周期。
  std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
  std::atomic<bool> stop_{false};
  std::thread thread_;

  // 主线程（initialize / exit）与接收线程共享的全局状态标志。
  bool curl_ready_ = false;

  // 仅接收线程使用。
  CURL* easy_ = nullptr;
  std::map<std::int64_t, AppInfo> apps_;  // appid → name/icon
  std::uint64_t apps_generation_ = 0;     // 已加载应用列表的配置代号
  std::string apps_error_;                // 上一次失败原因，避免重连时刷屏
};

bool GotifySource::initialize() {
  if (!curl_ready_) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      host()->set_error("libcurl 初始化失败");
      return false;
    }
    curl_ready_ = true;
  }
  return true;
}

Config GotifySource::ReadConfig() {
  Config cfg;
  const auto text = [this](const char* name, const char* fallback) -> std::string {
    const char* value = host()->setting_string(name, fallback);
    return (value != nullptr) ? value : fallback;
  };

  cfg.poll_interval_sec = host()->setting_int("poll_interval_sec", 2);
  if (cfg.poll_interval_sec < 1) cfg.poll_interval_sec = 1;
  if (cfg.poll_interval_sec > 60) cfg.poll_interval_sec = 60;

  cfg.min_priority = host()->setting_int("min_priority", -128);
  if (cfg.min_priority < -128) cfg.min_priority = -128;
  if (cfg.min_priority > 1024) cfg.min_priority = 1024;

  const std::int64_t content_kb = host()->setting_int("content_max_kb", 32);
  cfg.content_bytes = static_cast<std::size_t>(std::clamp<std::int64_t>(content_kb, 1, 48)) *
                      1024;

  const std::int64_t queue_max = host()->setting_int("queue_max", 256);
  cfg.queue_max =
      static_cast<std::size_t>(std::clamp<std::int64_t>(queue_max, 4, 4096));

  cfg.ping_interval_sec = host()->setting_int("ping_interval_sec", 30);
  if (cfg.ping_interval_sec < 0) cfg.ping_interval_sec = 0;
  if (cfg.ping_interval_sec > 3600) cfg.ping_interval_sec = 3600;

  cfg.use_priority_level = host()->setting_bool("use_priority_level", true);
  cfg.tls_verify = host()->setting_bool("tls_verify", true);
  cfg.include_extras = host()->setting_bool("include_extras", false);

  bool topic_changed = false;
  cfg.topic = SanitizeTopic(text("topic", "gotify.in"), &topic_changed);
  if (topic_changed) {
    host()->logf(LogLevel::Warn,
                 "topic 里含有不允许的字符（只允许小写字母、数字、下划线、点、减号），"
                 "已替换为下划线：现在用 \"%s\"",
                 cfg.topic.c_str());
  }

  const std::string server = text("server_url", "http://127.0.0.1:8080");
  const std::string path = text("stream_path", "/stream");
  cfg.client_token = Trim(text("client_token", ""));
  cfg.username = Trim(text("username", ""));
  cfg.password = text("password", "");
  cfg.client_name = Trim(text("client_name", "SimpleMessage"));
  if (cfg.client_name.empty()) cfg.client_name = "SimpleMessage";
  if (const char* data_dir = host()->data_dir(); data_dir != nullptr) {
    // 每个实例必须有自己的 token 缓存：文件名从自己的设置文件派生。
    // 首个实例仍得到旧的 source.gotify-client-token.json，@2 起各自独立。
    std::string stem = "source.gotify";
    if (const char* own = host()->own_config_path(); own != nullptr && *own != '\0') {
      std::string own_path(own);
      const std::size_t slash = own_path.find_last_of("/\\");
      stem = (slash == std::string::npos) ? own_path : own_path.substr(slash + 1);
      if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".toml") == 0) {
        stem.resize(stem.size() - 5);
      }
      if (stem.empty()) stem = "source.gotify";
    }
    cfg.token_cache_path = std::string(data_dir) + "/" + stem + "-client-token.json";
  }

  std::string problem;
  const bool endpoints_ok = BuildEndpoints(server, path, &cfg.http_base,
                                           &cfg.stream_base, &problem);
  cfg.display_url = cfg.stream_base;

  const bool has_login = !cfg.username.empty() && !cfg.password.empty();
  cfg.configured = endpoints_ok && (!cfg.client_token.empty() || has_login);
  if (endpoints_ok && !cfg.configured) {
    problem =
        "尚未配置认证信息：请填写 client_token，或同时填写 username 与 "
        "password。登录模式下插件会自动创建或复用名为 \"" +
        cfg.client_name + "\" 的客户端。";
  }
  cfg.problem = problem;

  // 密码也放进 identity：改密码后必须重新登录，不能沿用缓存里的旧令牌。
  cfg.identity = endpoints_ok
                     ? (cfg.stream_base + "\n" + cfg.username + "\n" +
                        cfg.client_name + "\n" + cfg.client_token + "\n" +
                        (cfg.tls_verify ? "1" : "0") + "\n" + cfg.password)
                     : (server + "\n" + cfg.username + "\n" + cfg.password);
  return cfg;
}

bool GotifySource::EnsureThread() {
  if (thread_started_) return true;
  stop_.store(false);
  try {
    thread_ = std::thread([this] { ListenLoop(); });
    thread_started_ = true;
    return true;
  } catch (const std::exception& e) {
    host()->set_error(e.what());
    return false;
  }
}

StartResult GotifySource::start(StartOutput* out) {
  Config cfg = ReadConfig();

  bool identity_changed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cfg.identity != current_identity_ || cfg.configured != current_configured_) {
      current_identity_ = cfg.identity;
      current_configured_ = cfg.configured;
      ++config_generation_;
      identity_changed = true;
    }
    cfg.generation = config_generation_;
    config_ = cfg;
  }

  if (cfg.problem != reported_problem_) {
    reported_problem_ = cfg.problem;
    if (!cfg.problem.empty()) {
      host()->log(LogLevel::Info, cfg.problem.c_str());
    }
  }
  if (identity_changed && cfg.configured) {
    host()->logf(LogLevel::Info, "配置已更新，将连接 %s", cfg.display_url.c_str());
  }

  if (!cfg.configured) {
    // 配置被清空后，别把上一次连接失败的错误留在插件页上。
    if (!reported_error_.empty()) {
      reported_error_.clear();
      host()->set_error(nullptr);
    }
    host()->schedule_next(cfg.poll_interval_sec * 1000);
    return StartResult::Empty;
  }

  if (!EnsureThread()) {
    host()->schedule_next(cfg.poll_interval_sec * 1000);
    return StartResult::Error;
  }

  std::deque<Pending> taken;
  std::uint64_t dropped = 0;
  std::uint64_t rejected = 0;
  std::uint64_t parse_failed = 0;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(queue_);
    dropped = dropped_;
    dropped_ = 0;
    rejected = rejected_;
    rejected_ = 0;
    parse_failed = parse_failed_;
    parse_failed_ = 0;
    error = last_error_;
  }

  if (error != reported_error_) {
    reported_error_ = error;
    host()->set_error(error.empty() ? nullptr : error.c_str());
  }
  if (rejected != 0) {
    host()->logf(LogLevel::Debug, "按 min_priority 过滤掉 %llu 条低优先级消息",
                 static_cast<unsigned long long>(rejected));
  }
  if (parse_failed != 0) {
    host()->logf(LogLevel::Warn, "有 %llu 条 Gotify 消息无法解析或过大，已丢弃",
                 static_cast<unsigned long long>(parse_failed));
  }

  host()->schedule_next(cfg.poll_interval_sec * 1000);

  if (taken.empty()) {
    if (dropped != 0) {
      host()->logf(LogLevel::Warn, "待投递队列已满，累计丢弃了 %llu 条最旧的消息",
                   static_cast<unsigned long long>(dropped));
    }
    return StartResult::Empty;
  }

  // arena 不够时不要把已经出队的消息丢掉：按原顺序放回队首，下次再试。
  const auto requeue_remaining = [this](std::deque<Pending>& items, std::size_t from) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = items.size(); i > from; --i) {
      queue_.push_front(std::move(items[i - 1]));
    }
  };

  sm::Notification* items = host()->allocate_array<sm::Notification>(taken.size());
  if (items == nullptr) {
    host()->logf(LogLevel::Warn,
                 "本次要投递 %llu 条消息，但 arena 分配失败，留待下次重试",
                 static_cast<unsigned long long>(taken.size()));
    requeue_remaining(taken, 0);
    return StartResult::Error;
  }

  std::size_t made = 0;
  for (Pending& p : taken) {
    sm::Notification& n = items[made];
    n = sm::Notification{};

    const std::size_t need =
        p.title.size() + 1 + p.content.size() + 1 + p.topic.size() + 1 +
        (p.id.empty() ? 0 : p.id.size() + 1) +
        (p.summary.empty() ? 0 : p.summary.size() + 1) +
        (p.icon.empty() ? 0 : p.icon.size() + 1);
    char* buf = static_cast<char*>(host()->allocate(need));
    if (buf == nullptr) break;

    char* w = buf;
    const auto put = [&w](const std::string& s) -> const char* {
      char* const start = w;
      if (!s.empty()) std::memcpy(w, s.data(), s.size());
      w += s.size();
      *w++ = '\0';
      return start;
    };
    n.title = put(p.title);
    n.content = put(p.content);
    n.topic = put(p.topic);
    n.id = p.id.empty() ? nullptr : put(p.id);
    n.summary = p.summary.empty() ? nullptr : put(p.summary);
    n.icon = p.icon.empty() ? nullptr : put(p.icon);

    n.level = p.level;
    n.time_unix_ms = p.time_ms;
    ++made;
  }

  if (made < taken.size()) {
    host()->logf(LogLevel::Warn,
                 "有 %llu 条消息因 arena 不足未能投递，已放回队列等待下次",
                 static_cast<unsigned long long>(taken.size() - made));
    requeue_remaining(taken, made);
  }

  if (made == 0) return StartResult::Error;

  out->items = sm::Span<const sm::Notification>(items, made);
  if (dropped != 0) {
    host()->logf(LogLevel::Info,
                 "本次投递 %llu 条 Gotify 通知（另有 %llu 条因队列满被丢弃）",
                 static_cast<unsigned long long>(made),
                 static_cast<unsigned long long>(dropped));
  } else {
    host()->logf(LogLevel::Info, "本次投递 %llu 条 Gotify 通知",
                 static_cast<unsigned long long>(made));
  }
  return StartResult::Ok;
}

bool GotifySource::OpenConnection(const std::string& stream_url, const Config& cfg,
                                  CURL** easy_out, long* http_status,
                                  std::string* error) {
  *http_status = 0;
  CURL* easy = curl_easy_init();
  if (easy == nullptr) {
    *error = "无法创建 libcurl 会话";
    return false;
  }

  char errbuf[CURL_ERROR_SIZE];
  errbuf[0] = '\0';

  curl_easy_setopt(easy, CURLOPT_URL, stream_url.c_str());
  curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket 模式
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "SimpleMessage/source.gotify");
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "ws,wss");
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, cfg.tls_verify ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, cfg.tls_verify ? 2L : 0L);

  const CURLcode code = curl_easy_perform(easy);
  // 握手失败时状态码仍有诊断价值：401 说明令牌无效或类型不对（例如误填了
  // gtfya. 开头的应用令牌），404 则是 URL/前缀不对。
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, http_status);
  if (code != CURLE_OK) {
    *error = (errbuf[0] != '\0') ? errbuf : curl_easy_strerror(code);
    if (*http_status != 0) {
      *error += "（HTTP " + std::to_string(*http_status) + "）";
    }
    curl_easy_cleanup(easy);
    return false;
  }

  *easy_out = easy;
  return true;
}

GotifySource::ReadStatus GotifySource::ReadOnce(CURL* easy, const Config& cfg,
                                                std::string* frame, bool* oversize,
                                                std::string* error) {
  char buffer[16 * 1024];
  std::size_t nread = 0;
  const curl_ws_frame* meta = nullptr;

  const CURLcode code = curl_ws_recv(easy, buffer, sizeof(buffer), &nread, &meta);
  if (code == CURLE_AGAIN) return ReadStatus::Again;
  if (code != CURLE_OK) {
    *error = (code == CURLE_GOT_NOTHING) ? "Gotify 关闭了连接"
                                         : curl_easy_strerror(code);
    return ReadStatus::Error;
  }
  if (meta == nullptr) return ReadStatus::Again;

  if (meta->flags & CURLWS_CLOSE) {
    *error = "Gotify 关闭了 WebSocket 连接";
    return ReadStatus::Error;
  }

  // PING / PONG 已被 libcurl 自动处理（自动回 PONG，不交给调用方）。
  if ((meta->flags & (CURLWS_TEXT | CURLWS_BINARY)) == 0) return ReadStatus::Data;

  if (!*oversize) {
    if (frame->size() + nread > kMaxMessageBytes) {
      *oversize = true;
      frame->clear();
    } else {
      frame->append(buffer, nread);
    }
  }

  // 分片消息要攒到最后一个分片；bytesleft 是"当前帧还剩多少"。
  const bool last_fragment =
      (meta->bytesleft == 0) && ((meta->flags & CURLWS_CONT) == 0);
  if (last_fragment) {
    if (*oversize) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++parse_failed_;
    } else {
      HandleMessage(*frame, cfg);
    }
    frame->clear();
    *oversize = false;
  }
  return ReadStatus::Data;
}

bool GotifySource::FetchApplications(const Config& cfg,
                                     const std::string& token,
                                     std::string* error) {
  // 令牌走 X-Gotify-Key 头，不要拼进 URL：URL 会进入 Debug 日志、错误文本
  // 和可能的代理访问日志。
  const std::string endpoint = cfg.http_base + "/application?token=" + token;
  HttpResponse response;
  std::string http_error;
  if (!HttpRequest(endpoint, cfg, HttpMethod::Get, std::string(), std::string(),
                   &response, &http_error))
  {
    *error = "获取应用列表失败（" + endpoint + "）：" + http_error;
    return false;
  }
  if (response.status == 401 || response.status == 403) {
    *error = "获取应用列表失败：客户端令牌无权读取应用（HTTP " +
             std::to_string(response.status) + "）";
    return false;
  }
  if (response.status != 200) {
    *error = "获取应用列表失败（HTTP " + std::to_string(response.status) +
             "）：" + BodySnippet(response.body);
    return false;
  }

  std::vector<AppRecord> records;
  if (!ParseApplicationArray(response.body, &records)) {
    *error = "获取应用列表失败：响应不是合法的应用数组";
    return false;
  }

  std::map<std::int64_t, AppInfo> apps;
  for (const AppRecord& record : records) {
    if (record.name.empty()) continue;
    AppInfo info;
    info.name = record.name;
    info.icon = AppIconUrl(cfg.http_base, record.image);
    apps[record.id] = std::move(info);
  }
  apps_.swap(apps);
  return true;
}

void GotifySource::HandleMessage(const std::string& json, const Config& cfg) {
  std::int64_t id = 0;
  std::int64_t appid = 0;
  std::int64_t priority = 0;
  const bool has_id = JsonTopLevelInt(json, "id", &id);
  const bool has_appid = JsonTopLevelInt(json, "appid", &appid);
  const bool has_priority = JsonTopLevelInt(json, "priority", &priority);
  if (!has_priority) priority = 0;

  if (priority < cfg.min_priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++rejected_;
    return;
  }

  std::string received_title;
  std::string message;
  std::string date;
  JsonTopLevelString(json, "title", &received_title);
  JsonTopLevelString(json, "message", &message);
  JsonTopLevelString(json, "date", &date);

  if (message.empty()) message = "(无正文)";

  if (cfg.include_extras) {
    std::string extras;
    if (JsonTopLevelRaw(json, "extras", &extras) && !extras.empty() &&
        extras != "null") {
      message += "\n\n--- extras ---\n";
      message += extras;
    }
  }

  Pending item;
  item.content = std::move(message);
  if (has_appid) {
    const auto app = apps_.find(appid);
    if (app != apps_.end() && !app->second.name.empty()) {
      // 应用名做标题；Gotify 消息自己的 title 降为简介，正文进详情。
      item.title = app->second.name;
      item.summary = "[" + received_title + "]  " + item.content.substr(0, 30);
      item.icon = app->second.icon;
    } else {
      item.title = received_title.empty()
                       ? ("Gotify 应用 " + std::to_string(appid))
                       : received_title;
    }
  } else {
    item.title = received_title.empty() ? "Gotify 消息" : received_title;
  }
  item.topic = cfg.topic;
  item.level = LevelForPriority(priority, cfg.use_priority_level);
  item.time_ms = host()->now_unix_ms();
  if (!date.empty()) {
    std::int64_t parsed = 0;
    if (ParseRfc3339Ms(date, &parsed)) item.time_ms = parsed;
  }
  if (has_id) item.id = "gotify:" + std::to_string(id);

  ClampUtf8(&item.title, kTitleBytes);
  ClampUtf8(&item.summary, kTitleBytes);
  ClampUtf8(&item.icon, sm::limits::kMaxIconBytes);
  ClampUtf8(&item.content, cfg.content_bytes);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= cfg.queue_max) {
      queue_.pop_front();  // 满了丢最旧的
      ++dropped_;
    }
    queue_.push_back(std::move(item));
  }

  host()->logf(LogLevel::Debug, "收到 Gotify 消息 id=%lld priority=%lld",
               static_cast<long long>(id), static_cast<long long>(priority));
}

void GotifySource::SendPing(CURL* easy) {
  static const char kEmpty[1] = {0};
  std::size_t sent = 0;
  const CURLcode code = curl_ws_send(easy, kEmpty, 0, &sent, 0, CURLWS_PING);
  if (code != CURLE_OK && code != CURLE_AGAIN) {
    host()->logf(LogLevel::Debug, "发送 WebSocket PING 失败：%s",
                 curl_easy_strerror(code));
  }
}

void GotifySource::ListenLoop() {
  std::int64_t backoff_sec = kReconnectMinSec;
  std::uint64_t active_generation = 0;
  int failures = 0;
  std::int64_t next_ping_ms = 0;
  std::string frame;
  bool oversize = false;
  std::string cached_token;         // 登录模式下按 generation 缓存的 Client Token
  std::uint64_t token_generation = 0;
  int auth_retries = 0;  // 缓存令牌被 401 拒绝后，已重新登录的次数

  while (!stop_.load()) {
    const Config cfg = SnapshotConfig();

    if (!cfg.configured) {
      if (easy_ != nullptr) {
        curl_easy_cleanup(easy_);
        easy_ = nullptr;
      }
      active_generation = 0;
      apps_.clear();
      apps_generation_ = 0;
      apps_error_.clear();
      SetLastError(std::string());
      SleepInterruptible(500);
      continue;
    }

    if (easy_ == nullptr || active_generation != cfg.generation) {
      if (easy_ != nullptr) {
        curl_easy_cleanup(easy_);
        easy_ = nullptr;
      }
      active_generation = cfg.generation;
      auth_retries = 0;
      SetLastError(std::string());

      std::string error;
      std::string stream_url;
      std::string active_token;
      bool ready = true;
      if (cfg.client_token.empty()) {
        // 登录模式：同一个 generation 内复用已获取的令牌，避免每次重连都登录。
        if (token_generation != cfg.generation) {
          std::string token;
          bool from_cache = false;
          if (!ObtainClientToken(cfg, &token, &from_cache, &error)) {
            ready = false;
          } else {
            cached_token = token;
            token_generation = cfg.generation;
            if (from_cache) {
              host()->logf(LogLevel::Info,
                           "已登录 Gotify，使用缓存的客户端令牌 \"%s\"",
                           cfg.client_name.c_str());
            } else {
              host()->logf(LogLevel::Info,
                           "已登录 Gotify，已创建客户端 \"%s\" 并缓存令牌",
                           cfg.client_name.c_str());
            }
          }
        }
        if (ready) {
          active_token = cached_token;
          stream_url = cfg.stream_base + "?token=" + UrlEncode(cached_token);
        }
      } else {
        active_token = cfg.client_token;
        stream_url = cfg.stream_base + "?token=" + UrlEncode(cfg.client_token);
      }

      // 每次配置代号变化（启动、改服务器、改账号/令牌）都先取一次应用列表，
      // 这样后续消息才能把 appid 映射成应用名与图标。失败不阻塞收消息。
      if (ready && apps_generation_ != cfg.generation) {
        std::string apps_error;
        if (FetchApplications(cfg, active_token, &apps_error)) {
          apps_generation_ = cfg.generation;
          apps_error_.clear();
          host()->logf(LogLevel::Info, "已获取 %llu 个 Gotify 应用",
                       static_cast<unsigned long long>(apps_.size()));
        } else {
          apps_.clear();
          if (apps_error != apps_error_) {
            apps_error_ = apps_error;
            host()->log(LogLevel::Warn, apps_error.c_str());
          } else {
            host()->log(LogLevel::Debug, apps_error.c_str());
          }
        }
      }

      long http_status = 0;
      if (!ready ||
          !OpenConnection(stream_url, cfg, &easy_, &http_status, &error)) {
        // 缓存的令牌被服务端拒绝：删掉缓存，下一轮重新登录。只重试一次，
        // 避免密码也错时每轮都新建一个客户端。
        if (ready && http_status == 401 && cfg.client_token.empty()) {
          if (auth_retries == 0) {
            ClearCachedToken(cfg);
            cached_token.clear();
            token_generation = 0;
            ++auth_retries;
            host()->log(LogLevel::Warn,
                        "Gotify 拒绝了客户端令牌，已清除本地缓存；下一轮将重新登录");
          } else {
            host()->log(LogLevel::Warn,
                        "重新登录后 Gotify 仍拒绝客户端令牌，请检查 server_url"
                        "、用户名与密码");
          }
          error = "客户端令牌无效（HTTP 401）";
        } else if (ready && http_status == 401 && failures == 0) {
          host()->log(LogLevel::Warn,
                      "Gotify 拒绝了这个令牌：/stream 需要客户端令牌（gtfyc. 开头）"
                      "，不能使用应用令牌（gtfya. 开头）；也可以清空 client_token"
                      " 改用用户名与密码登录");
        }
        ++failures;
        SetLastError(error);
        if (failures == 1) {
          host()->logf(LogLevel::Warn,
                       "连接 Gotify 失败：%s（%lld 秒后重试）", error.c_str(),
                       static_cast<long long>(backoff_sec));
        } else {
          host()->logf(LogLevel::Debug,
                       "重连 Gotify 仍失败：%s（%lld 秒后重试）", error.c_str(),
                       static_cast<long long>(backoff_sec));
        }
        SleepInterruptible(backoff_sec * 1000);
        backoff_sec = std::min(backoff_sec * 2, kReconnectMaxSec);
        continue;
      }

      backoff_sec = kReconnectMinSec;
      if (failures > 0) {
        host()->logf(LogLevel::Info, "已重新连上 %s", cfg.display_url.c_str());
      } else {
        host()->logf(LogLevel::Info, "已连接 %s", cfg.display_url.c_str());
      }
      failures = 0;
      auth_retries = 0;
      frame.clear();
      oversize = false;
      next_ping_ms = (cfg.ping_interval_sec > 0)
                         ? host()->now_unix_ms() + cfg.ping_interval_sec * 1000
                         : 0;
      continue;
    }

    const std::int64_t now = host()->now_unix_ms();
    if (next_ping_ms != 0 && now >= next_ping_ms) {
      SendPing(easy_);
      next_ping_ms = now + cfg.ping_interval_sec * 1000;
    }

    std::string error;
    const ReadStatus status = ReadOnce(easy_, cfg, &frame, &oversize, &error);
    if (status == ReadStatus::Data) continue;
    if (status == ReadStatus::Again) {
      SleepInterruptible(kReadPollMs);
      continue;
    }

    if (stop_.load()) break;

    ++failures;
    SetLastError(error);
    if (failures == 1) {
      host()->logf(LogLevel::Warn, "与 Gotify 的连接断开：%s（%lld 秒后重连）",
                   error.c_str(), static_cast<long long>(backoff_sec));
    } else {
      host()->logf(LogLevel::Debug, "与 Gotify 的连接仍然断开：%s（%lld 秒后重连）",
                   error.c_str(), static_cast<long long>(backoff_sec));
    }

    curl_easy_cleanup(easy_);
    easy_ = nullptr;
    active_generation = 0;
    SleepInterruptible(backoff_sec * 1000);
    backoff_sec = std::min(backoff_sec * 2, kReconnectMaxSec);
  }

  if (easy_ != nullptr) {
    curl_easy_cleanup(easy_);
    easy_ = nullptr;
  }
}

Config GotifySource::SnapshotConfig() {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void GotifySource::SetLastError(const std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_error_ = error;
}

void GotifySource::SleepInterruptible(std::int64_t ms) {
  if (ms <= 0) return;
  std::unique_lock<std::mutex> lock(wait_mutex_);
  wait_cv_.wait_for(lock, std::chrono::milliseconds(ms),
                    [this] { return stop_.load(); });
}

void GotifySource::StopListener() {
  if (!thread_started_) return;
  stop_.store(true);
  wait_cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  thread_started_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  last_error_.clear();
}

void GotifySource::stop() {
  StopListener();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void GotifySource::exit() {
  StopListener();
  if (curl_ready_) {
    curl_global_cleanup();
    curl_ready_ = false;
  }
}

}  // namespace

SM_DEFINE_PLUGIN(kMeta, GotifySource);

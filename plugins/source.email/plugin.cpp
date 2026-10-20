// SimpleMessage 示例插件 —— source.email

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
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
using sm::SettingOption;
using sm::StartOutput;
using sm::StartResult;

constexpr std::size_t kResponseBytes = 512 * 1024;

constexpr SettingOption kSecurityOptions[] = {
    {"imaps", "IMAPS（993，全程加密）"},
    {"starttls", "IMAP + STARTTLS（143 起，随后升级加密）"},
    {"plain", "IMAP 明文（仅供本机调试）"},
};

constexpr SettingDef kSettings[] = {
    sm::setting::text("host", "", 128, "IMAP 服务器",
                      "如 imap.qq.com / outlook.office365.com。留空表示不取邮件"
                      "（插件会空转并说明原因，不会静默什么都不做）。"),
    sm::setting::integer("port", 993, 1, 65535, "端口"),
    sm::setting::enumeration("security", "imaps", sm::span_of(kSecurityOptions),
                             "加密方式"),
    sm::setting::text("username", "", 128, "账号",
                      "通常是完整邮箱地址，视服务商而定。"),
    sm::setting::text("password", "", 256, "密码或授权码",
                      "【明文保存在插件配置文件里】—— 本版本没有系统凭据库集成。"
                      "多数邮箱需要的是【授权码/应用专用密码】，"
                      "不是登录密码，请先在服务商那边生成一个。"),
    sm::setting::text("folder", "INBOX", 128, "邮箱文件夹",
                      "IMAP 里的文件夹名，通常是 INBOX。"),
    sm::setting::integer("interval_sec", 300, 30, 86400, "轮询间隔（秒）",
                         "每次执行失败都按这个值做基数的指数退避"
                         "（第一次失败等 2×间隔，上限 30 分钟）。"),
    sm::setting::integer("max_messages", 10, 1, 100, "单次最多取几封",
                         "限制一次执行里最多取几封邮件。剩下的会在下一次执行"
                         "时继续（未读状态本身没变，所以不会漏）。"),
    sm::setting::integer("run_budget_sec", 20, 5, 25, "单次执行时间预算（秒）",
                         "一次 start() 最多花这么久，超过就带着已取到的结果"
                         "返回。看门狗阈值是 30 秒，所以这个值不允许超过 25。"),
    sm::setting::integer("content_max_kb", 32, 1, 48, "正文上限（KB）",
                         "超长邮件会被截断到这个大小。"),
    sm::setting::boolean("tls_verify", true, "校验服务器证书",
                         "【只在调试时才考虑关闭】关掉它等于放弃对中间人攻击的"
                         "防护。自签名证书请改用系统信任链，而不是关这个开关。"),
    sm::setting::boolean("mark_seen", false, "取到后标记为已读",
                         "打开后本插件会在报告一封邮件之后把它标为已读"
                         "（IMAP UID STORE +FLAGS \\Seen）。默认关闭："
                         "让邮件在你自己的客户端里保持未读。"),
    sm::setting::text("topic", "mail.inbox", 128, "通知主题",
                      "通知的 topic，用于路由。只允许小写字母、数字、下划线、"
                      "点、减号，其它字符会被替换成下划线并记 Warn。"),
};

constexpr PluginMeta kMeta = {
     "source.email",
     "邮件接收",
     "SimpleMessage 示例",
     "1.0.0",
     "用 libcurl 直连 IMAP/IMAPS 邮箱，把未读邮件变成通知。",
     PluginKind::Source,
     ">=0.1.0,<1.0.0",
     sm::Span<const char*>{},
     sm::span_of(kSettings),
     SM_API_VERSION,
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string Trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// 宿主是按【字节】截断的，插件自己截的时候必须停在 UTF-8 码点边界上，
// 否则中文会被切成半个字。
void ClampUtf8(std::string* s, std::size_t max_bytes) {
  if (s->size() <= max_bytes) return;
  std::size_t i = max_bytes;
  while (i > 0 && (static_cast<unsigned char>((*s)[i]) & 0xC0) == 0x80) --i;
  s->resize(i);
}

void NormalizeNewlines(std::string* s) {
  std::string out;
  out.reserve(s->size());
  for (std::size_t i = 0; i < s->size(); ++i) {
    if ((*s)[i] == '\r') {
      if (i + 1 < s->size() && (*s)[i + 1] == '\n') continue;
      out.push_back('\n');
      continue;
    }
    out.push_back((*s)[i]);
  }
  *s = out;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool Base64Decode(const std::string& in, std::string* out) {
  auto index_of = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  int buf = 0;
  int bits = -8;
  for (char c : in) {
    if (IsSpace(c) || c == '=') continue;
    const int v = index_of(c);
    if (v < 0) return false;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 0) {
      out->push_back(static_cast<char>((buf >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return true;
}

bool QuotedPrintableDecode(const std::string& in, bool encoded_word, std::string* out) {
  for (std::size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (encoded_word && c == '_') {
      out->push_back(' ');
      continue;
    }
    if (c != '=') {
      out->push_back(c);
      continue;
    }
    if (i + 1 < in.size() && (in[i + 1] == '\n' || in[i + 1] == '\r')) {
      ++i;
      if (in[i] == '\r' && i + 1 < in.size() && in[i + 1] == '\n') ++i;
      continue;
    }
    if (i + 2 >= in.size()) return false;
    const int hi = HexValue(in[i + 1]);
    const int lo = HexValue(in[i + 2]);
    if (hi < 0 || lo < 0) return false;
    out->push_back(static_cast<char>((hi << 4) | lo));
    i += 2;
  }
  return true;
}

bool ToUtf8(const std::string& raw, const std::string& charset, std::string* out) {
  const std::string cs = ToLower(Trim(charset));
  if (cs.empty() || cs == "utf-8" || cs == "utf8" || cs == "us-ascii" ||
      cs == "ascii") {
    *out = raw;
    return true;
  }
  if (cs == "iso-8859-1" || cs == "latin1" || cs == "iso8859-1") {
    out->clear();
    out->reserve(raw.size() * 2);
    for (unsigned char c : raw) {
      if (c < 0x80) {
        out->push_back(static_cast<char>(c));
      } else {
        out->push_back(static_cast<char>(0xC0 | (c >> 6)));
        out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
      }
    }
    return true;
  }
  return false;
}

std::string DecodeEncodedWords(const std::string& in) {
  std::string out;
  std::size_t i = 0;
  bool last_was_encoded = false;
  while (i < in.size()) {
    if (in[i] == '=' && i + 1 < in.size() && in[i + 1] == '?') {
      const std::size_t q1 = in.find('?', i + 2);
      const std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : in.find('?', q1 + 1);
      const std::size_t end = (q2 == std::string::npos) ? std::string::npos : in.find("?=", q2 + 1);
      if (end == std::string::npos) {
        out.push_back(in[i++]);
        last_was_encoded = false;
        continue;
      }
      const std::string charset = in.substr(i + 2, q1 - (i + 2));
      const std::string encoding = ToLower(in.substr(q1 + 1, q2 - (q1 + 1)));
      const std::string text = in.substr(q2 + 1, end - (q2 + 1));

      std::string bytes;
      bool ok = false;
      if (encoding == "b") {
        ok = Base64Decode(text, &bytes);
      } else if (encoding == "q") {
        ok = QuotedPrintableDecode(text, true, &bytes);
      }
      std::string utf8;
      if (ok) ok = ToUtf8(bytes, charset, &utf8);
      if (!ok) {
        out += in.substr(i, end + 2 - i);
      } else {
        if (last_was_encoded) {
          while (!out.empty() && out.back() == ' ') out.pop_back();
        }
        out += utf8;
      }
      i = end + 2;
      last_was_encoded = true;
      continue;
    }
    out.push_back(in[i++]);
    last_was_encoded = false;
  }
  return out;
}

std::string StripHtml(const std::string& html) {
  std::string out;
  out.reserve(html.size() / 2);
  bool in_tag = false;
  for (char c : html) {
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '>') {
      in_tag = false;
      continue;
    }
    if (!in_tag) out.push_back(c);
  }
  return out;
}

struct Mail {
  std::string subject;
  std::string from;
  std::string date;
  std::string body;
  std::string charset_note;
};

using Headers = std::map<std::string, std::string>;

Headers ParseHeaders(const std::string& block) {
  // 折行的续行要接回"上一个字段名"，所以必须自己记 last_name —— Headers 是
  // std::map，按 key 排序，rbegin() 不是"最后插入的那条"。同名头部只取第一个。
  Headers headers;
  std::string last_name;
  std::size_t i = 0;
  while (i < block.size()) {
    std::size_t eol = block.find('\n', i);
    if (eol == std::string::npos) eol = block.size();
    std::string line = block.substr(i, eol - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    i = (eol == block.size()) ? block.size() : eol + 1;

    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      if (!last_name.empty()) headers[last_name] += Trim(line);
      continue;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
      last_name.clear();
      continue;
    }
    const std::string name = ToLower(Trim(line.substr(0, colon)));
    if (name.empty()) {
      last_name.clear();
      continue;
    }
    const std::string value = Trim(line.substr(colon + 1));
    if (headers.find(name) == headers.end()) headers[name] = value;
    last_name = name;
  }
  return headers;
}

void SplitHeaderBody(const std::string& chunk, std::string* headers, std::string* body) {
  const std::size_t crlf = chunk.find("\r\n\r\n");
  const std::size_t lf = chunk.find("\n\n");
  if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf)) {
    *headers = chunk.substr(0, crlf);
    *body = chunk.substr(crlf + 4);
    return;
  }
  if (lf != std::string::npos) {
    *headers = chunk.substr(0, lf);
    *body = chunk.substr(lf + 2);
    return;
  }
  *headers = chunk;
  body->clear();
}

std::string ContentTypeParam(const std::string& content_type, const char* key) {
  const std::string lower = ToLower(content_type);
  const std::string k = ToLower(key) + "=";
  std::size_t pos = 0;
  while ((pos = lower.find(k, pos)) != std::string::npos) {
    const bool at_start = (pos == 0) || lower[pos - 1] == ';' || IsSpace(lower[pos - 1]);
    if (!at_start) {
      pos += k.size();
      continue;
    }
    const std::size_t v = pos + k.size();
    if (v >= content_type.size()) return std::string();
    if (content_type[v] == '"') {
      const std::size_t end = content_type.find('"', v + 1);
      if (end == std::string::npos) return std::string();
      return content_type.substr(v + 1, end - (v + 1));
    }
    std::size_t end = content_type.find(';', v);
    if (end == std::string::npos) end = content_type.size();
    return Trim(content_type.substr(v, end - v));
  }
  return std::string();
}

bool DecodeBodyPart(const std::string& raw, const std::string& cte, std::string* out) {
  const std::string e = ToLower(Trim(cte));
  if (e == "base64") return Base64Decode(raw, out);
  if (e == "quoted-printable") return QuotedPrintableDecode(raw, false, out);
  if (e.empty() || e == "7bit" || e == "8bit" || e == "binary") {
    *out = raw;
    return true;
  }
  return false;
}

bool PickMultipartPart(const std::string& body, const std::string& boundary,
                       std::string* part_headers, std::string* part_body,
                       bool* part_is_html) {
  if (boundary.empty()) return false;
  const std::string marker = "--" + boundary;
  std::size_t pos = body.find(marker);
  if (pos == std::string::npos) return false;

  std::vector<std::pair<std::string, std::string>> parts;
  while (pos != std::string::npos) {

    if (pos + marker.size() + 2 <= body.size() &&
        body.compare(pos + marker.size(), 2, "--") == 0) {
      break;
    }
    std::size_t head = body.find('\n', pos);
    if (head == std::string::npos) break;
    ++head;
    const std::size_t next = body.find("\n" + marker, head);
    const std::size_t end = (next == std::string::npos) ? body.size() : next;
    std::string piece_headers;
    std::string piece_body;
    SplitHeaderBody(body.substr(head, end - head), &piece_headers, &piece_body);
    parts.emplace_back(std::move(piece_headers), std::move(piece_body));
    pos = (next == std::string::npos) ? std::string::npos : next + 1;
  }
  if (parts.empty()) return false;

  for (int pass = 0; pass < 2; ++pass) {
    for (const auto& part : parts) {
      const Headers h = ParseHeaders(part.first);
      auto it = h.find("content-type");
      if (it == h.end()) continue;
      const std::string ct = ToLower(it->second);
      const bool is_plain = (ct.find("text/plain") == 0);
      const bool is_html = (ct.find("text/html") == 0);
      if ((pass == 0 && is_plain) || (pass == 1 && is_html)) {
        *part_headers = part.first;
        *part_body = part.second;
        *part_is_html = is_html;
        return true;
      }
    }
  }
  *part_headers = parts[0].first;
  *part_body = parts[0].second;
  *part_is_html = false;
  return true;
}

Mail ParseMail(const std::string& raw) {
  Mail mail;
  std::string header_block;
  std::string body;
  SplitHeaderBody(raw, &header_block, &body);
  NormalizeNewlines(&header_block);
  const Headers headers = ParseHeaders(header_block);

  const auto get = [&headers](const char* name) -> std::string {
    auto it = headers.find(name);
    return (it == headers.end()) ? std::string() : it->second;
  };
  mail.subject = DecodeEncodedWords(get("subject"));
  mail.from = DecodeEncodedWords(get("from"));
  mail.date = get("date");

  std::string content_type = get("content-type");
  if (content_type.empty()) content_type = "text/plain";
  std::string cte = get("content-transfer-encoding");
  bool is_html = (ToLower(content_type).find("text/html") == 0);

  if (ToLower(content_type).find("multipart/") == 0) {
    const std::string boundary = ContentTypeParam(content_type, "boundary");
    std::string part_headers;
    std::string part_body;
    if (PickMultipartPart(body, boundary, &part_headers, &part_body, &is_html)) {
      const Headers ph = ParseHeaders(part_headers);
      auto it_type = ph.find("content-type");
      auto it_enc = ph.find("content-transfer-encoding");
      content_type = (it_type == ph.end()) ? std::string("text/plain") : it_type->second;
      cte = (it_enc == ph.end()) ? std::string() : it_enc->second;
      body = part_body;
    }
  }

  std::string decoded;
  if (!DecodeBodyPart(body, cte, &decoded)) {
    decoded = body;
    mail.charset_note = "（正文的传输编码是 \"" + Trim(cte) + "\"，无法解码，以下为原始内容）";
  }
  const std::string charset = ContentTypeParam(content_type, "charset");
  std::string utf8;
  if (!ToUtf8(decoded, charset, &utf8)) {
    utf8 = decoded;
    if (!mail.charset_note.empty()) mail.charset_note += "\n";
    mail.charset_note += "（正文字符集是 \"" + Trim(charset) +
                         "\"，本插件只认 UTF-8 / ISO-8859-1，以下为原始字节）";
  }
  mail.body = is_html ? StripHtml(utf8) : utf8;
  NormalizeNewlines(&mail.body);
  return mail;
}

struct Transfer {
  const sm::IHostContext* host = nullptr;
  std::int64_t deadline_ms = 0;
  std::string* out = nullptr;
  std::size_t limit = kResponseBytes;
  bool cancelled = false;
  bool truncated = false;
};

struct TransferContext {
  Transfer transfer;
  std::string buffer;
  std::string error;
};

int XferInfoCallback(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  Transfer* t = static_cast<Transfer*>(user);
  if (t->host->is_cancelled()) {
    t->cancelled = true;
    return 1;
  }
  if (t->deadline_ms > 0 && t->host->now_unix_ms() > t->deadline_ms) {
    t->cancelled = true;
    return 1;
  }
  return 0;
}

std::size_t WriteCallback(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
  Transfer* t = static_cast<Transfer*>(user);
  const std::size_t total = size * nmemb;
  if (!t->truncated && t->limit > 0 && t->out->size() < t->limit) {
    const std::size_t room = t->limit - t->out->size();
    const std::size_t take = (total < room) ? total : room;
    t->out->append(ptr, take);
    if (take < total) t->truncated = true;
  } else {
    t->truncated = true;
  }
  // 必须返回 total 而不是 take：返回短值会被 curl 报成 CURLE_WRITE_ERROR。
  return total;
}

bool ParseSearchResponse(const std::string& response, std::vector<long>* uids) {
  std::size_t i = 0;
  while (i < response.size()) {
    std::size_t eol = response.find('\n', i);
    if (eol == std::string::npos) eol = response.size();
    const std::string line = Trim(response.substr(i, eol - i));
    i = (eol == response.size()) ? response.size() : eol + 1;

    const std::string lower = ToLower(line);
    if (lower.size() < 8 || lower.compare(0, 8, "* search") != 0) continue;

    std::size_t p = 8;
    while (p < line.size()) {
      while (p < line.size() && IsSpace(line[p])) ++p;
      if (p >= line.size() || line[p] < '0' || line[p] > '9') break;
      long value = 0;
      while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
        value = value * 10 + (line[p] - '0');
        ++p;
      }
      uids->push_back(value);
    }
    return true;
  }
  return false;
}

std::string SanitizeTopic(const std::string& raw, bool* changed) {
  std::string out = ToLower(raw);
  *changed = false;
  if (out.empty()) {
    out = "mail.inbox";
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

class EmailSource final : public PluginBase {
 public:
  explicit EmailSource(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override;
  StartResult start(StartOutput* out) override;
  void exit() override;

 private:
  struct Config {
    std::string host;
    long port = 993;
    std::string security;
    std::string username;
    std::string password;
    std::string folder;
    long interval_sec = 300;
    long max_messages = 10;
    long run_budget_sec = 20;
    long content_max_kb = 32;
    bool tls_verify = true;
    bool mark_seen = false;
    std::string topic;
  };

  Config ReadConfig() const;

  CURLcode Request(CURL* easy, const Config& cfg, const std::string& url,
                   const char* custom_request, std::int64_t deadline_ms,
                   std::string* response, std::string* detail, bool* cancelled);

  CURL* easy_ = nullptr;
  bool curl_ready_ = false;
  std::uint64_t run_count_ = 0;
  std::set<long> reported_;
};

bool EmailSource::initialize() {
  if (!curl_ready_) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      host()->set_error("libcurl 初始化失败");
      return false;
    }
    curl_ready_ = true;
  }
  return true;
}

void EmailSource::exit() {
  if (easy_ != nullptr) {
    curl_easy_cleanup(easy_);
    easy_ = nullptr;
  }
  if (curl_ready_) {
    curl_global_cleanup();
    curl_ready_ = false;
  }
}

EmailSource::Config EmailSource::ReadConfig() const {
  Config cfg;
  const auto text = [this](const char* name, const char* fallback) -> std::string {
    const char* value = host()->setting_string(name, fallback);
    return (value != nullptr) ? value : fallback;
  };
  cfg.host = text("host", "");
  cfg.port = static_cast<long>(host()->setting_int("port", 993));
  cfg.security = text("security", "imaps");
  cfg.username = text("username", "");
  cfg.password = text("password", "");
  cfg.folder = text("folder", "INBOX");
  cfg.interval_sec = static_cast<long>(host()->setting_int("interval_sec", 300));
  cfg.max_messages = static_cast<long>(host()->setting_int("max_messages", 10));
  cfg.run_budget_sec = static_cast<long>(host()->setting_int("run_budget_sec", 20));
  cfg.content_max_kb = static_cast<long>(host()->setting_int("content_max_kb", 32));
  cfg.tls_verify = host()->setting_bool("tls_verify", true);
  cfg.mark_seen = host()->setting_bool("mark_seen", false);

  if (cfg.interval_sec < 30) cfg.interval_sec = 30;
  if (cfg.max_messages < 1) cfg.max_messages = 1;
  if (cfg.run_budget_sec < 5) cfg.run_budget_sec = 5;
  if (cfg.run_budget_sec > 25) cfg.run_budget_sec = 25;  // 宿主的看门狗阈值是 30 秒，预算必须留在它之内
  if (cfg.content_max_kb < 1) cfg.content_max_kb = 1;
  if (cfg.content_max_kb > 48) cfg.content_max_kb = 48;

  bool changed = false;
  const char* topic_setting = host()->setting_string("topic", "mail.inbox");
  cfg.topic = SanitizeTopic((topic_setting != nullptr) ? topic_setting : "mail.inbox",
                            &changed);
  if (changed) {
    host()->logf(LogLevel::Warn,
                 "topic 里含有不允许的字符（只允许小写字母、数字、下划线、点、减号），"
                 "已替换为下划线：现在用 \"%s\"",
                 cfg.topic.c_str());
  }
  return cfg;
}

CURLcode EmailSource::Request(CURL* easy, const Config& cfg, const std::string& url,
                              const char* custom_request, std::int64_t deadline_ms,
                              std::string* response, std::string* detail,
                              bool* cancelled) {
  TransferContext context;
  context.transfer.host = host();
  context.transfer.deadline_ms = deadline_ms;
  context.transfer.out = response;
  context.transfer.limit = kResponseBytes;
  context.error.resize(CURL_ERROR_SIZE, '\0');

  curl_easy_reset(easy);

  long timeout_s = static_cast<long>((deadline_ms - host()->now_unix_ms()) / 1000);
  if (timeout_s < 1) timeout_s = 1;

  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  if (!cfg.username.empty()) {
    curl_easy_setopt(easy, CURLOPT_USERNAME, cfg.username.c_str());
    curl_easy_setopt(easy, CURLOPT_PASSWORD, cfg.password.c_str());
  }
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, context.error.data());
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteCallback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &context.transfer);
  curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &XferInfoCallback);
  curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &context.transfer);
  curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "imap,imaps");
  if (custom_request != nullptr) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, custom_request);
  }
  if (ToLower(cfg.security) == "starttls") {
    curl_easy_setopt(easy, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
  } else if (ToLower(cfg.security) == "imaps") {
    curl_easy_setopt(easy, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
  }
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, cfg.tls_verify ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, cfg.tls_verify ? 2L : 0L);

  const CURLcode code = curl_easy_perform(easy);
  *cancelled = context.transfer.cancelled;
  if (code == CURLE_OK && context.transfer.truncated) {
    host()->logf(LogLevel::Warn, "IMAP 响应超过 %llu 字节，多出来的部分被丢弃",
                 static_cast<unsigned long long>(kResponseBytes));
  }
  if (code == CURLE_OK) return code;

  *detail = context.error[0] != '\0' ? context.error : curl_easy_strerror(code);
  return code;
}

StartResult EmailSource::start(StartOutput* out) {
  const Config cfg = ReadConfig();
  const bool first_run = (run_count_ == 0);
  ++run_count_;

  if (cfg.host.empty()) {
    if (first_run) {
      host()->log(LogLevel::Info,
                  "尚未配置 IMAP 服务器（host 为空），本插件不会取任何邮件。"
                  "在控制面板的插件页填上服务器地址与账号即可。");
    } else {
      host()->log(LogLevel::Debug, "host 为空，本次空转");
    }
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Empty;
  }

  if (easy_ == nullptr) {
    easy_ = curl_easy_init();
    if (easy_ == nullptr) {
      host()->set_error("无法创建 libcurl 会话");
      host()->schedule_next(cfg.interval_sec * 1000);
      return StartResult::Error;
    }
  }

  const std::int64_t deadline_ms = host()->now_unix_ms() + cfg.run_budget_sec * 1000;

  const std::string base = (ToLower(cfg.security) == "imaps" ? "imaps://" : "imap://") +
                           cfg.host + ":" + std::to_string(cfg.port) + "/" + cfg.folder;

  std::string search_response;
  std::string detail;
  bool cancelled = false;
  const CURLcode search_code = Request(easy_, cfg, base, "UID SEARCH UNSEEN", deadline_ms,
                                       &search_response, &detail, &cancelled);
  if (cancelled) {
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Cancelled;
  }
  if (search_code != CURLE_OK) {
    char text[512];
    std::snprintf(text, sizeof(text), "查询未读邮件失败：%s", detail.c_str());
    host()->set_error(text);
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Error;
  }

  std::vector<long> uids;
  if (!ParseSearchResponse(search_response, &uids)) {
    std::string head = search_response.substr(0, 200);
    NormalizeNewlines(&head);
    host()->logf(LogLevel::Warn, "IMAP 响应里没有 SEARCH 行，原始响应开头：%s",
                 head.c_str());
    host()->set_error("IMAP 服务器没有返回搜索结果（文件夹名或权限可能不对）");
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Error;
  }

  // 已读/删除的 UID 不会再出现在 UNSEEN 里；不裁剪的话 reported_ 会随
  // 邮箱使用无限增长。搜索成功才裁剪，避免一次失败的 SEARCH 清空记录后
  // 把所有旧邮件重新报一遍。
  const std::set<long> unseen(uids.begin(), uids.end());
  for (auto it = reported_.begin(); it != reported_.end();) {
    if (unseen.find(*it) == unseen.end()) {
      it = reported_.erase(it);
    } else {
      ++it;
    }
  }

  // 搜索成功说明连接与认证正常，清掉上一轮的错误提示。
  host()->set_error(nullptr);

  std::vector<long> fresh;
  for (long uid : uids) {
    if (reported_.find(uid) == reported_.end()) fresh.push_back(uid);
  }
  const std::size_t unseen_total = uids.size();
  const std::size_t already = unseen_total - fresh.size();

  if (fresh.empty()) {
    host()->logf(LogLevel::Debug, "本次轮询：未读 %llu 封，都是已报过的，没有新邮件",
                 static_cast<unsigned long long>(unseen_total));
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Empty;
  }

  const std::size_t want =
      (fresh.size() > static_cast<std::size_t>(cfg.max_messages))
          ? static_cast<std::size_t>(cfg.max_messages)
          : fresh.size();

  const std::size_t body_limit = static_cast<std::size_t>(cfg.content_max_kb) * 1024;
  sm::Notification* items = host()->allocate_array<sm::Notification>(want);
  if (items == nullptr) {
    host()->set_error("arena 分配失败，本次不投递");
    host()->schedule_next(cfg.interval_sec * 1000);
    return StartResult::Error;
  }

  std::size_t made = 0;
  std::size_t fetch_failures = 0;
  std::string last_failure;
  for (std::size_t i = 0; i < want; ++i) {
    if (host()->is_cancelled() || host()->now_unix_ms() > deadline_ms) {
      host()->logf(LogLevel::Info, "本次执行的时间预算用完了，已取 %llu 封，剩下的下一轮继续",
                   static_cast<unsigned long long>(made));
      break;
    }

    const std::string url = base + ";UID=" + std::to_string(fresh[i]);
    std::string raw;
    bool abort_now = false;
    const CURLcode code = Request(easy_, cfg, url, nullptr, deadline_ms, &raw, &detail,
                                  &abort_now);
    if (abort_now) break;
    if (code != CURLE_OK) {
      ++fetch_failures;
      last_failure = detail;
      host()->logf(LogLevel::Warn, "取 UID %ld 失败：%s（跳过这一封）", fresh[i],
                   detail.c_str());
      continue;
    }

    const Mail mail = ParseMail(raw);
    sm::Notification& n = items[made];
    n = sm::Notification{};

    std::string title = mail.subject.empty() ? std::string("（无主题）") : mail.subject;
    ClampUtf8(&title, 200);

    std::string content;
    content.reserve(mail.body.size() + 128);
    if (!mail.charset_note.empty()) {
      content += mail.charset_note;
      content += "\n";
    }
    content += "发件人：";
    content += mail.from.empty() ? std::string("（未提供）") : mail.from;
    if (!mail.date.empty()) {
      content += "\n时间：";
      content += mail.date;
    }
    content += "\n\n";
    content += mail.body;
    ClampUtf8(&content, body_limit);

    const std::size_t need = title.size() + content.size() + cfg.topic.size() + 3;
    char* buf = static_cast<char*>(host()->allocate(need));
    if (buf == nullptr) {
      host()->logf(LogLevel::Warn, "arena 分配失败，UID %ld 这一封跳过", fresh[i]);
      ++fetch_failures;
      continue;
    }
    char* w = buf;
    const auto put = [&w](const std::string& s) {
      std::memcpy(w, s.data(), s.size());
      w += s.size();
      *w++ = '\0';
    };
    put(title);
    n.title = buf;
    put(content);
    n.content = buf + title.size() + 1;
    put(cfg.topic);
    n.topic = buf + title.size() + content.size() + 2;

    n.level = sm::Level::Info;
    n.time_unix_ms = host()->now_unix_ms();
    n.summary = nullptr;
    n.id = nullptr;
    n.url = nullptr;
    n.actions = sm::Span<sm::Action>{};

    reported_.insert(fresh[i]);
    ++made;

    if (cfg.mark_seen) {
      const std::string store_cmd =
          "UID STORE " + std::to_string(fresh[i]) + " +FLAGS (\\Seen)";
      std::string ignored;
      bool store_cancelled = false;
      std::string store_detail;
      const CURLcode store_code = Request(easy_, cfg, base, store_cmd.c_str(), deadline_ms,
                                          &ignored, &store_detail, &store_cancelled);
      if (store_code != CURLE_OK) {
        host()->logf(LogLevel::Warn, "标记 UID %ld 为已读失败：%s", fresh[i],
                     store_detail.c_str());
      }
    }
  }

  host()->schedule_next(cfg.interval_sec * 1000);

  if (made == 0) {
    if (fetch_failures > 0) {
      char text[512];
      std::snprintf(text, sizeof(text), "有未读邮件但一封都没取到（最后一封的原因：%s）",
                    last_failure.c_str());
      host()->set_error(text);
      return StartResult::Error;
    }
    host()->log(LogLevel::Debug, "本次没有可取的新邮件");
    return StartResult::Empty;
  }

  out->items = sm::Span<const sm::Notification>(items, made);
  if (fetch_failures > 0) {
    host()->logf(LogLevel::Info,
                 "本次投递 %llu 封新邮件（未读合计 %llu 封，其中 %llu 封此前已报过；"
                 "另有 %llu 封本次取失败）",
                 static_cast<unsigned long long>(made),
                 static_cast<unsigned long long>(unseen_total),
                 static_cast<unsigned long long>(already),
                 static_cast<unsigned long long>(fetch_failures));
  } else {
    host()->logf(LogLevel::Info, "本次投递 %llu 封新邮件（未读合计 %llu 封，其中 %llu 封此前已报过）",
                 static_cast<unsigned long long>(made),
                 static_cast<unsigned long long>(unseen_total),
                 static_cast<unsigned long long>(already));
  }
  return StartResult::Ok;
}

}  // namespace

SM_DEFINE_PLUGIN(kMeta, EmailSource);

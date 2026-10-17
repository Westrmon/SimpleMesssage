// SimpleMessage 示例插件 —— source.webhook

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#pragma warning(push, 0)
#include <httplib.h>
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

constexpr std::size_t kHardMaxBodyBytes = 1024 * 1024;
constexpr std::size_t kTitleBytes = 200;
constexpr std::size_t kContentBytes = 60 * 1024;

constexpr SettingDef kSettings[] = {
    sm::setting::integer("port", 8721, 1024, 65535, "监听端口",
                         "接收端口。改端口会在下一次调度时重建监听 —— "
                         "不需要重启插件，但会有最多一个轮询间隔的空窗。"),
    sm::setting::text("bind_addr", "127.0.0.1", 64, "绑定地址",
                      "默认只监听本机回环地址。改成 0.0.0.0 会让【同一局域网里"
                      "的任何机器】都能往这里投递 —— 那时请务必同时设置令牌。"),
    sm::setting::text("path", "/hook", 128, "接收路径",
                      "只接收这个路径上的 POST。留空表示任何路径都接收。"),
    sm::setting::text("token", "", 128, "访问令牌",
                      "留空表示不校验。非空时要求请求头 X-SimpleMessage-Token"
                      "（头名可改）或查询参数 ?token= 与之完全一致，"
                      "否则拒绝并记 Warn。"),
    sm::setting::text("token_header", "X-SimpleMessage-Token", 64, "令牌所在请求头",
                      "只在令牌非空时有意义。"),
    sm::setting::text("topic", "webhook.in", 128, "通知主题",
                      "通知的 topic，用于路由。只允许小写字母、数字、下划线与点，"
                      "其它字符会被替换成下划线并记 Warn。"),
    sm::setting::integer("poll_interval_sec", 5, 1, 3600, "投递间隔（秒）",
                         "收到的请求最多攒这么久才变成通知。通知只能在 start() "
                         "里产生，所以这个值就是从收到到看见的延迟上限。"),
    sm::setting::integer("max_body_kb", 16, 1, 1024, "请求体上限（KB）",
                         "超过就拒绝（HTTP 413）。"),
    sm::setting::integer("queue_max", 256, 4, 4096, "待投递队列上限",
                         "队列满时丢弃【最旧】的请求并计数（丢最新的会让用户"
                         "永远看不到当前发生的事）。"),
    sm::setting::boolean("include_headers", false, "把请求头并入内容",
                         "打开后通知的 content 会在请求体前面附上请求头，"
                         "便于排查对方到底发了什么。"),
};

constexpr PluginMeta kMeta = {
     "source.webhook",
     "Webhook 接收",
     "SimpleMessage 示例",
     "1.0.0",
     "在本机开一个 HTTP 接收端口，把收到的 POST 变成通知。",
     PluginKind::Source,
     ">=0.1.0,<1.0.0",
     sm::Span<const char*>{},
     sm::span_of(kSettings),
     SM_API_VERSION,
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
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

bool JsonTopLevelString(const std::string& body, const char* key, std::string* out) {
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

    if (name.size() == key_len && name.compare(key) == 0) {
      if (i < body.size() && body[i] == '"') return ScanJsonString(body, &i, out);
      return false;
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

void ClampUtf8(std::string* s, std::size_t max_bytes) {
  if (s->size() <= max_bytes) return;
  std::size_t i = max_bytes;
  while (i > 0 && (static_cast<unsigned char>((*s)[i]) & 0xC0) == 0x80) --i;
  s->resize(i);
}

std::string FirstLine(const std::string& s) {
  std::size_t begin = 0;
  while (begin < s.size()) {
    const std::size_t eol = s.find('\n', begin);
    const std::size_t end = (eol == std::string::npos) ? s.size() : eol;
    std::size_t e = end;
    while (e > begin && IsSpace(s[e - 1])) --e;
    if (e > begin) return s.substr(begin, e - begin);
    begin = (eol == std::string::npos) ? s.size() : eol + 1;
  }
  return std::string();
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string SanitizeTopic(const std::string& raw, bool* changed) {
  std::string out = ToLower(raw);
  *changed = false;
  if (out.empty()) {
    out = "webhook.in";
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

sm::Level LevelFromName(const std::string& name, bool* found) {
  const std::string v = ToLower(name);
  *found = true;
  if (v == "debug") return sm::Level::Debug;
  if (v == "info") return sm::Level::Info;
  if (v == "warning" || v == "warn") return sm::Level::Warning;
  if (v == "error") return sm::Level::Error;
  if (v == "critical" || v == "fatal") return sm::Level::Critical;
  *found = false;
  return sm::Level::Info;
}

bool PathAccepted(const std::string& pattern, bool pattern_is_regex,
                  const std::shared_ptr<const std::regex>& pattern_regex,
                  const std::string& path) {
  if (pattern.empty()) return true;
  if (!pattern_is_regex) return path == pattern;
  return pattern_regex != nullptr && std::regex_match(path, *pattern_regex);
}

// 这张元字符表必须与 httplib 的 make_matcher 完全一致 —— 只留这一份定义。
constexpr const char* kRegexMetaChars = ".^$|()[]{}*+?\\";

bool PortAlreadyAnswersHttp(const std::string& host, int port) {
  const std::string target = (host == "0.0.0.0") ? "127.0.0.1" : host;
  httplib::Client probe(target, port);
  probe.set_connection_timeout(0, 300000);
  probe.set_read_timeout(0, 500000);
  return probe.Get("/") != nullptr;
}

struct Pending {
  std::string title;
  std::string content;
  std::string topic;
  std::string icon;
  sm::Level level = sm::Level::Info;
  std::int64_t time_ms = 0;
};

struct RequestConfig {
  std::string path;
  std::string token;
  std::string token_header;
  std::string topic;
  bool include_headers = false;
  std::size_t max_body_bytes = 0;
  std::int64_t queue_max = 256;
};

// 收到的请求不能立刻投递：SDK 没有"随时投递"接口，通知只能在 start() 里产生。
// 于是监听线程只往一把锁保护的队列里放东西（除 log/logf/now_unix_ms 外不碰宿主），
// 由 start() 定期取走。exit() 返回前必须 join 掉监听线程 —— 宿主随后就 FreeLibrary。
class WebhookSource final : public PluginBase {
 public:
  explicit WebhookSource(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  StartResult start(StartOutput* out) override;
  void stop() override;
  void exit() override;

 private:
  bool StartListener(const std::string& addr, int port, std::size_t max_body_bytes,
                     std::string* error);
  void StopListener();

  void ListenLoop();

  void HandleRequest(const httplib::Request& req, httplib::Response& res);

  RequestConfig SnapshotConfig();

  std::unique_ptr<httplib::Server> server_;
  std::thread listen_thread_;
  std::string listen_addr_;
  std::string listen_path_;
  int listen_port_ = 0;

  std::mutex mutex_;
  RequestConfig config_;
  std::deque<Pending> queue_;
  std::uint64_t dropped_ = 0;
  std::uint64_t received_ = 0;
  std::uint64_t rejected_ = 0;
};

StartResult WebhookSource::start(StartOutput* out) {
  const std::int64_t interval_sec = host()->setting_int("poll_interval_sec", 5);
  const std::int64_t port = host()->setting_int("port", 8721);
  const char* addr_setting = host()->setting_string("bind_addr", "127.0.0.1");
  const std::string addr =
      (addr_setting != nullptr && addr_setting[0] != '\0') ? addr_setting : "127.0.0.1";
  std::size_t max_body = static_cast<std::size_t>(host()->setting_int("max_body_kb", 16)) *
                         1024;
  if (max_body == 0 || max_body > kHardMaxBodyBytes) max_body = kHardMaxBodyBytes;

  const char* topic_setting = host()->setting_string("topic", "webhook.in");
  bool topic_changed = false;
  std::string topic = SanitizeTopic(
      (topic_setting != nullptr) ? topic_setting : "webhook.in", &topic_changed);
  if (topic_changed) {
    host()->logf(LogLevel::Warn,
                 "topic 里含有不允许的字符（只允许小写字母、数字、下划线、点、减号），"
                 "已替换为下划线：现在用 \"%s\"",
                 topic.c_str());
  }
  const char* path_setting = host()->setting_string("path", "/hook");
  const std::string path_value = (path_setting != nullptr) ? path_setting : "";
  const char* token_setting = host()->setting_string("token", "");
  const char* token_header_setting =
      host()->setting_string("token_header", "X-SimpleMessage-Token");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.path = path_value;
    config_.token = (token_setting != nullptr) ? token_setting : "";
    config_.token_header =
        (token_header_setting != nullptr && token_header_setting[0] != '\0')
            ? token_header_setting
            : "X-SimpleMessage-Token";
    config_.topic = topic;
    config_.include_headers = host()->setting_bool("include_headers", false);
    config_.max_body_bytes = max_body;
    config_.queue_max = host()->setting_int("queue_max", 256);
  }

  // path 也要比：路由模式在注册时就已经定死，改了 path 必须重建监听。
  if (server_ && (listen_port_ != static_cast<int>(port) || listen_addr_ != addr ||
                  listen_path_ != path_value)) {
    host()->logf(LogLevel::Info,
                 "监听参数已改变（%s:%d%s -> %s:%d%s），重建接收端口",
                 listen_addr_.c_str(), listen_port_,
                 listen_path_.empty() ? "/<任意路径>" : listen_path_.c_str(),
                 addr.c_str(), static_cast<int>(port),
                 path_value.empty() ? "/<任意路径>" : path_value.c_str());
    StopListener();
  }
  if (!server_) {
    std::string error;
    if (!StartListener(addr, static_cast<int>(port), max_body, &error)) {
      host()->set_error(error.c_str());
      host()->schedule_next(interval_sec * 1000);
      return StartResult::Error;
    }
  }

  std::deque<Pending> taken;
  std::uint64_t dropped = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(queue_);
    dropped = dropped_;
    dropped_ = 0;
  }

  if (taken.empty()) {
    if (dropped != 0) {
      host()->logf(LogLevel::Warn, "待投递队列已满，累计丢弃了 %llu 条最旧的请求",
                   static_cast<unsigned long long>(dropped));
    }
    host()->schedule_next(interval_sec * 1000);
    return StartResult::Empty;
  }

  const std::size_t count = taken.size();
  sm::Notification* items = host()->allocate_array<sm::Notification>(count);
  if (items == nullptr) {
    host()->logf(LogLevel::Warn, "本次要投递 %llu 条请求，但 arena 分配失败，丢弃",
                 static_cast<unsigned long long>(count));
    host()->schedule_next(interval_sec * 1000);
    return StartResult::Empty;
  }

  std::size_t made = 0;
  for (std::size_t i = 0; i < count; ++i) {
    Pending& p = taken[i];
    sm::Notification& n = items[made];
    n = sm::Notification{};

    const std::size_t icon_bytes = p.icon.empty() ? 0 : p.icon.size() + 1;
    const std::size_t need =
        p.title.size() + p.content.size() + p.topic.size() + icon_bytes + 3;
    char* buf = static_cast<char*>(host()->allocate(need));
    if (buf == nullptr) break;
    char* w = buf;
    const auto put = [&w](const std::string& s) {
      std::memcpy(w, s.data(), s.size());
      w += s.size();
      *w++ = '\0';
    };
    put(p.title);
    n.title = buf;
    put(p.content);
    n.content = buf + p.title.size() + 1;
    put(p.topic);
    n.topic = buf + p.title.size() + p.content.size() + 2;
    if (!p.icon.empty()) {
      n.icon = buf + p.title.size() + p.content.size() + p.topic.size() + 3;
      put(p.icon);
    }

    n.level = p.level;
    n.time_unix_ms = p.time_ms;
    n.summary = nullptr;
    ++made;
  }

  if (made == 0) {
    host()->logf(LogLevel::Warn, "有 %llu 条请求待投递，但 arena 全部分配失败，全部丢弃",
                 static_cast<unsigned long long>(count));
    host()->schedule_next(interval_sec * 1000);
    return StartResult::Empty;
  }

  out->items = sm::Span<const sm::Notification>(items, made);
  host()->schedule_next(interval_sec * 1000);
  if (dropped != 0) {
    host()->logf(LogLevel::Info, "本次投递 %llu 条 webhook 通知（另有 %llu 条因队列满被丢弃）",
                 static_cast<unsigned long long>(made),
                 static_cast<unsigned long long>(dropped));
  } else {
    host()->logf(LogLevel::Info, "本次投递 %llu 条 webhook 通知",
                 static_cast<unsigned long long>(made));
  }
  return StartResult::Ok;
}

bool WebhookSource::StartListener(const std::string& addr, int port,
                                  std::size_t max_body_bytes, std::string* error) {
  auto* server = new httplib::Server();

  server->new_task_queue = [] { return new httplib::ThreadPool(2, 4); };
  server->set_payload_max_length(max_body_bytes);
  server->set_read_timeout(10);
  server->set_write_timeout(10);
  server->set_keep_alive_timeout(1);
  server->set_exception_handler(
      [this](const httplib::Request& req, httplib::Response& res,
             std::exception_ptr ep) {
        std::string what = "未知异常";
        try {
          std::rethrow_exception(ep);
        } catch (const std::exception& e) {
          what = e.what();
        } catch (...) {
        }
        host()->logf(LogLevel::Warn, "处理 %s %s 时抛出异常：%s", req.method.c_str(),
                     req.path.c_str(), what.c_str());
        res.status = 500;
        res.set_content("internal error\n", "text/plain");
      });

  const std::string pattern = config_.path;
  const bool pattern_is_regex =
      pattern.find_first_of(kRegexMetaChars) != std::string::npos;
  std::shared_ptr<const std::regex> pattern_regex;
  if (pattern_is_regex) {
    try {
      pattern_regex = std::make_shared<const std::regex>(pattern);
    } catch (const std::regex_error& e) {
      delete server;
      if (error != nullptr) {
        char text[320];
        std::snprintf(text, sizeof(text),
                      "设置 path（\"%s\"）不是合法的正则：%s。"
                      "含正则元字符的路径会被当作正则，请修正或去掉这些字符。",
                      pattern.c_str(), e.what());
        *error = text;
      }
      return false;
    }
  }

  server->set_pre_routing_handler([this, pattern, pattern_is_regex, pattern_regex](
                                      const httplib::Request& req,
                                      httplib::Response&) {
    if (req.method == "POST" &&
        PathAccepted(pattern, pattern_is_regex, pattern_regex, req.path)) {
      return httplib::Server::HandlerResponse::Unhandled;
    }
    host()->logf(LogLevel::Warn,
                 "本次请求没有被接收：%s %s（当前只接收 POST %s，来自 %s）",
                 req.method.c_str(), req.path.c_str(),
                 pattern.empty() ? "<任意路径>" : pattern.c_str(),
                 req.remote_addr.c_str());
    return httplib::Server::HandlerResponse::Unhandled;
  });

  const std::string route = pattern.empty() ? std::string(".*") : pattern;
  server->Post(route, [this](const httplib::Request& req, httplib::Response& res) {
    HandleRequest(req, res);
  });

  // 先探一次再绑：Windows 上 httplib 会给监听套接字设 SO_REUSEADDR，两个进程能绑到
  // 同一个端口互相抢连接，bind 失败的分支根本走不到。探到只记 Warn 不拦 ——
  // 拦了会破坏"改设置重建监听"这条路径。
  if (PortAlreadyAnswersHttp(addr, port)) {
    host()->logf(LogLevel::Warn,
                 "端口 %s:%d 上已经有东西在应答 HTTP（可能是另一个 SimpleMessage "
                 "实例，或别的服务）。仍然会尝试绑定 —— 在 Windows 上两个监听者"
                 "会互相抢连接，请换一个端口，或先关掉那个程序。",
                 addr.c_str(), port);
  }

  if (!server->bind_to_port(addr, port)) {
    delete server;
    if (error != nullptr) {
      char text[256];
      std::snprintf(text, sizeof(text),
                    "无法监听 %s:%d（端口被占用，或地址不可用，或权限不足）",
                    addr.c_str(), port);
      *error = text;
    }
    return false;
  }

  server_.reset(server);
  listen_addr_ = addr;
  listen_port_ = port;
  listen_path_ = config_.path;
  listen_thread_ = std::thread([this] { ListenLoop(); });

  host()->logf(LogLevel::Info, "已开始监听 http://%s:%d%s（令牌：%s）", addr.c_str(), port,
               config_.path.empty() ? "/<任意路径>" : config_.path.c_str(),
               config_.token.empty() ? "不校验" : "已设置");
  return true;
}

void WebhookSource::ListenLoop() {
  server_->listen_after_bind();
}

void WebhookSource::StopListener() {
  if (server_) server_->stop();
  if (listen_thread_.joinable()) listen_thread_.join();
  server_.reset();
  listen_addr_.clear();
  listen_port_ = 0;
  listen_path_.clear();
}

void WebhookSource::stop() {
  StopListener();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void WebhookSource::exit() {
  StopListener();
}

RequestConfig WebhookSource::SnapshotConfig() {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void WebhookSource::HandleRequest(const httplib::Request& req, httplib::Response& res) {
  const RequestConfig cfg = SnapshotConfig();

  if (!cfg.token.empty()) {
    const std::string from_header = req.get_header_value(cfg.token_header, "");
    const std::string from_query = req.has_param("token") ? req.get_param_value("token") : "";
    if (from_header != cfg.token && from_query != cfg.token) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++rejected_;
      }
      host()->logf(LogLevel::Warn, "拒绝 %s %s：令牌不匹配（来自 %s）",
                   req.method.c_str(), req.path.c_str(), req.remote_addr.c_str());
      res.status = 401;
      res.set_content("unauthorized\n", "text/plain");
      return;
    }
  }

  const std::size_t body_limit =
      (cfg.max_body_bytes > 0) ? cfg.max_body_bytes : kHardMaxBodyBytes;
  if (req.body.size() > body_limit) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++rejected_;
    }
    host()->logf(LogLevel::Warn, "拒绝 %s %s：请求体 %llu 字节，超过上限 %llu 字节",
                 req.method.c_str(), req.path.c_str(),
                 static_cast<unsigned long long>(req.body.size()),
                 static_cast<unsigned long long>(body_limit));
    res.status = 413;
    res.set_content("payload too large\n", "text/plain");
    return;
  }

  Pending item;
  item.time_ms = host()->now_unix_ms();
  item.topic = cfg.topic;

  std::string body_title;
  if (JsonTopLevelString(req.body, "title", &body_title)) {
    item.title = body_title;
  } else {
    item.title = FirstLine(req.body);
  }
  if (item.title.empty()) {
    item.title = req.method + " " + req.path;
  }

  std::string body_level;
  bool level_found = false;
  if (JsonTopLevelString(req.body, "level", &body_level)) {
    item.level = LevelFromName(body_level, &level_found);
    if (!level_found) item.level = sm::Level::Info;
  }

  std::string body_icon;
  if (JsonTopLevelString(req.body, "icon", &body_icon)) {
    item.icon = body_icon;
    ClampUtf8(&item.icon, sm::limits::kMaxIconBytes);
  }

  if (cfg.include_headers) {
    item.content = "来自 ";
    item.content += req.remote_addr;
    item.content += ':';
    item.content += std::to_string(req.remote_port);
    item.content += "  ";
    item.content += req.method;
    item.content += ' ';
    item.content += req.target.empty() ? req.path : req.target;
    item.content += "\n--- 请求头 ---\n";
    for (const auto& kv : req.headers) {
      item.content += "  ";
      item.content += kv.first;
      item.content += ": ";
      item.content += kv.second;
      item.content += '\n';
    }
    item.content += "--- 请求体 ---\n";
  }
  item.content += req.body;

  ClampUtf8(&item.title, kTitleBytes);
  ClampUtf8(&item.content, kContentBytes);

  bool dropped_oldest = false;
  const std::int64_t cap = (cfg.queue_max > 0) ? cfg.queue_max : 256;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++received_;
    if (static_cast<std::int64_t>(queue_.size()) >= cap) {
      queue_.pop_front();  // 满了丢最旧的，并记数
      ++dropped_;
      dropped_oldest = true;
    }
    queue_.push_back(std::move(item));
  }
  if (dropped_oldest) {
    host()->logf(LogLevel::Warn, "待投递队列已满（上限 %lld），丢弃最旧的一条",
                 static_cast<long long>(cap));
  }

  host()->logf(LogLevel::Debug, "收到 %s %s（%llu 字节，来自 %s）", req.method.c_str(),
               req.path.c_str(), static_cast<unsigned long long>(req.body.size()),
               req.remote_addr.c_str());

  res.set_header("Connection", "close");
  res.status = 202;
  res.set_content("queued\n", "text/plain");
}

}  // namespace

SM_DEFINE_PLUGIN(kMeta, WebhookSource);

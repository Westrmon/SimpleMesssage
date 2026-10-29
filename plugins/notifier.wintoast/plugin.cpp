// SimpleMessage 插件 —— notifier.wintoast
//
// 用 WinToast 把通知投递成 Windows 原生 Toast。WinToast 的 instance() 是
// thread_local，且它会在首次初始化时创建开始菜单快捷方式，所以全部调用都放在
// 本插件自己的线程上；回调只入队，不碰宿主。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#pragma warning(push, 0)
#include "wintoastlib.h"
#pragma warning(pop)

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"
#include "sm/summary.h"

namespace {

using sm::LogLevel;
using sm::PluginBase;
using sm::PluginKind;
using sm::PluginMeta;
using sm::SettingDef;
using sm::SettingOption;
using WinToastLib::WinToast;
using WinToastLib::WinToastTemplate;

constexpr SettingOption kMinLevelOptions[] = {
    {"debug", "Debug 及以上（全部）"},
    {"info", "Info 及以上"},
    {"warning", "Warning 及以上"},
    {"error", "Error 及以上"},
    {"critical", "仅 Critical"},
};

constexpr SettingOption kDurationOptions[] = {
    {"system", "系统默认"},
    {"short", "短（约 5 秒）"},
    {"long", "长（约 25 秒）"},
};

constexpr SettingDef kSettings[] = {
    sm::setting::boolean("enabled", true, "启用原生通知",
                         "关掉后本插件不再弹出 Toast，通知照常由其它 Notifier 处理。"),
    sm::setting::enumeration("min_level", "info", sm::span_of(kMinLevelOptions),
                             "最低弹出级别",
                             "低于该级别的通知不弹 Toast。默认 Info 及以上；"
                             "Debug 通常是轮询类插件的常态输出。"),
    sm::setting::enumeration("duration", "system", sm::span_of(kDurationOptions),
                             "显示时长",
                             "short / long 会覆盖系统的默认停留时间；"
                             "system 使用 Windows 的默认值。"),
    sm::setting::boolean("silent", false, "静音",
                         "打开后 Toast 不播放提示音，也不影响其它 Notifier 的声音。"),
    sm::setting::boolean("show_actions", true, "显示动作按钮",
                         "把通知声明的 Action 画成 Toast 上的按钮（最多四个）。"
                         "关掉后点击 Toast 正文仍可打开链接。"),
};

constexpr PluginMeta kMeta = {
    "notifier.wintoast",
    "Windows 原生通知",
    "SimpleMessage 示例",
    "1.0.0",
    "使用 WinToast 把通知投递成 Windows 原生的 Toast 通知。",
    PluginKind::Notifier,
    ">=0.1.0,<1.0.0",
    sm::Span<const char*>{},
    sm::span_of(kSettings),
    SM_API_VERSION,
};

constexpr std::size_t kTitleBufferBytes = 512;
constexpr std::size_t kBodyBufferBytes = 1024;
constexpr std::size_t kMaxTitleCodepoints = 80;
constexpr std::size_t kMaxBodyCodepoints = 300;

int MinLevelThreshold(const char* value) {
  if (value != nullptr) {
    if (std::strcmp(value, "debug") == 0) return static_cast<int>(sm::Level::Debug);
    if (std::strcmp(value, "info") == 0) return static_cast<int>(sm::Level::Info);
    if (std::strcmp(value, "warning") == 0) return static_cast<int>(sm::Level::Warning);
    if (std::strcmp(value, "error") == 0) return static_cast<int>(sm::Level::Error);
    if (std::strcmp(value, "critical") == 0) return static_cast<int>(sm::Level::Critical);
  }
  return static_cast<int>(sm::Level::Info);
}

const char* ToastErrorText(WinToast::WinToastError error) {
  switch (error) {
    case WinToast::NoError:              return "无错误";
    case WinToast::NotInitialized:       return "WinToast 未初始化";
    case WinToast::SystemNotSupported:   return "系统不支持 Toast";
    case WinToast::ShellLinkNotCreated:  return "无法创建开始菜单快捷方式";
    case WinToast::InvalidAppUserModelID:return "AppUserModelID 无效";
    case WinToast::InvalidParameters:    return "参数无效";
    case WinToast::InvalidHandler:       return "通知回调无效";
    case WinToast::NotDisplayed:         return "系统拒绝显示通知";
    case WinToast::UnknownError:         return "未知错误";
  }
  return "未知错误";
}

std::wstring Utf8ToWide(const char* text) {
  if (text == nullptr || text[0] == '\0') return std::wstring();
  const int units = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (units <= 1) return std::wstring();
  std::wstring out(static_cast<std::size_t>(units), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), units);
  out.resize(static_cast<std::size_t>(units - 1));
  return out;
}

struct PendingToast {
  std::int64_t seq = 0;
  std::string title;
  std::string body;
  std::string url;
  std::vector<std::string> action_ids;
  std::vector<std::string> action_labels;
};

struct Click {
  std::int64_t seq = 0;
  std::string action_id;
  std::string url;
};

struct Sink {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::variant<PendingToast, Click>> events;
  bool alive = true;
  bool stopping = false;
};

class ToastHandler final : public WinToastLib::IWinToastHandler {
 public:
  ToastHandler(std::shared_ptr<Sink> sink, std::int64_t seq, std::string url,
               std::vector<std::string> actions)
      : sink_(std::move(sink)), seq_(seq), url_(std::move(url)),
        actions_(std::move(actions)) {}

  void toastActivated() const override {
    Click click;
    click.seq = seq_;
    if (url_.empty()) {
      click.action_id = "details";
    } else {
      click.url = url_;
    }
    Push(std::move(click));
  }

  void toastActivated(int action_index) const override {
    if (action_index < 0 ||
        static_cast<std::size_t>(action_index) >= actions_.size()) {
      return;
    }
    Click click;
    click.seq = seq_;
    click.action_id = actions_[static_cast<std::size_t>(action_index)];
    Push(std::move(click));
  }

  void toastActivated(std::wstring) const override {}
  void toastDismissed(WinToastDismissalReason) const override {}
  void toastFailed() const override {}

 private:
  void Push(Click click) const {
    {
      std::lock_guard<std::mutex> lock(sink_->mutex);
      if (!sink_->alive || sink_->stopping) return;
      sink_->events.emplace_back(std::move(click));
    }
    sink_->cv.notify_one();
  }

  std::shared_ptr<Sink> sink_;
  std::int64_t seq_ = 0;
  std::string url_;
  std::vector<std::string> actions_;
};

struct ClickTask {
  sm::IHostContext* host = nullptr;
  std::int64_t seq = 0;
  std::string action_id;
  std::string url;
};

void DeliverClickOnMain(void* user) {
  std::unique_ptr<ClickTask> task(static_cast<ClickTask*>(user));
  if (!task->url.empty()) {
    task->host->open_url(task->url.c_str());
    return;
  }
  if (!task->action_id.empty()) {
    task->host->invoke_action(task->seq, task->action_id.c_str());
  }
}

class WinToastNotifier final : public PluginBase {
 public:
  explicit WinToastNotifier(sm::IHostContext* host) : PluginBase(host, kMeta) {}
  ~WinToastNotifier() override { StopWorker(); }

  bool initialize() override {
    if (worker_.joinable()) return true;
    sink_ = std::make_shared<Sink>();
    try {
      worker_ = std::thread([this, sink = sink_] { WorkerLoop(sink); });
    } catch (const std::exception& e) {
      host()->logf(LogLevel::Warn, "无法启动原生通知线程：%s", e.what());
      sink_.reset();
      return false;
    }
    return true;
  }

  void on_notify(const sm::Notification& n) override {
    if (!host()->setting_bool("enabled", true)) return;
    if (static_cast<int>(n.level) <
        MinLevelThreshold(host()->setting_enum("min_level", "info"))) {
      return;
    }

    PendingToast toast;
    toast.seq = n.seq;

    char title[kTitleBufferBytes];
    sm::make_summary(n.title, kMaxTitleCodepoints, title, sizeof(title));
    toast.title = title;

    const char* body_source = (n.content != nullptr && n.content[0] != '\0')
                                  ? n.content
                                  : n.summary;
    char body[kBodyBufferBytes];
    sm::make_summary(body_source, kMaxBodyCodepoints, body, sizeof(body));
    toast.body = body;

    if (n.url != nullptr) toast.url = n.url;

    for (std::size_t i = 0;
         n.actions.data != nullptr && i < n.actions.size &&
         i < sm::limits::kMaxActionsPerNotification;
         ++i) {
      const sm::Action& action = n.actions[i];
      if (action.id == nullptr || action.id[0] == '\0') continue;
      toast.action_ids.emplace_back(action.id);
      toast.action_labels.emplace_back(
          (action.label != nullptr && action.label[0] != '\0') ? action.label
                                                               : action.id);
    }

    Enqueue(std::move(toast));
  }

  void stop() override { StopWorker(); }
  void exit() override { StopWorker(); }

 private:
  void Enqueue(PendingToast toast) {
    const std::shared_ptr<Sink> sink = sink_;
    if (!sink) return;
    {
      std::lock_guard<std::mutex> lock(sink->mutex);
      if (!sink->alive || sink->stopping) return;
      sink->events.emplace_back(std::move(toast));
    }
    sink->cv.notify_one();
  }

  void StopWorker() {
    const std::shared_ptr<Sink> sink = std::move(sink_);
    if (sink) {
      {
        std::lock_guard<std::mutex> lock(sink->mutex);
        sink->alive = false;
        sink->stopping = true;
        sink->events.clear();
      }
      sink->cv.notify_all();
    }
    if (worker_.joinable()) worker_.join();
    win_toast_ready_ = false;
    win_toast_failed_ = false;
    last_show_error_ = -1;
  }

  void WorkerLoop(const std::shared_ptr<Sink>& sink) {
    for (;;) {
      std::variant<PendingToast, Click> item;
      {
        std::unique_lock<std::mutex> lock(sink->mutex);
        sink->cv.wait(lock,
                      [&sink] { return sink->stopping || !sink->events.empty(); });
        if (sink->stopping) break;
        item = std::move(sink->events.front());
        sink->events.pop_front();
      }

      try {
        if (auto* toast = std::get_if<PendingToast>(&item)) {
          ShowToast(*toast, sink);
        } else {
          DeliverClick(std::get<Click>(item));
        }
      } catch (const std::exception& e) {
        host()->logf(LogLevel::Warn, "处理原生通知时发生异常：%s", e.what());
      } catch (...) {
        host()->log(LogLevel::Warn, "处理原生通知时发生未知异常。");
      }
    }

    if (win_toast_ready_) {
      WinToast::instance()->clear();
    }
  }

  bool EnsureWinToast() {
    if (win_toast_ready_) return true;
    if (win_toast_failed_) return false;

    WinToastLib::setDebugOutputEnabled(false);
    WinToast* const toast = WinToast::instance();
    toast->setAppName(L"SimpleMessage Notifications");
    toast->setAppUserModelId(
        WinToast::configureAUMI(L"SimpleMessage", L"NotifierWinToast"));
    toast->setShortcutPolicy(WinToast::SHORTCUT_POLICY_REQUIRE_CREATE);

    WinToast::WinToastError error = WinToast::NoError;
    if (!toast->initialize(&error)) {
      win_toast_failed_ = true;
      host()->logf(LogLevel::Warn, "无法初始化 Windows 原生通知：%s。",
                   ToastErrorText(error));
      return false;
    }
    win_toast_ready_ = true;
    return true;
  }

  void ShowToast(const PendingToast& toast, const std::shared_ptr<Sink>& sink) {
    if (!EnsureWinToast()) return;

    WinToastTemplate tmpl(WinToastTemplate::Text02);
    tmpl.setFirstLine(Utf8ToWide(toast.title.c_str()));
    tmpl.setSecondLine(Utf8ToWide(toast.body.c_str()));

    const char* const duration = host()->setting_enum("duration", "system");
    if (std::strcmp(duration, "short") == 0) {
      tmpl.setDuration(WinToastTemplate::Duration::Short);
    } else if (std::strcmp(duration, "long") == 0) {
      tmpl.setDuration(WinToastTemplate::Duration::Long);
    }

    if (host()->setting_bool("silent", false)) {
      tmpl.setAudioOption(WinToastTemplate::AudioOption::Silent);
    }

    if (host()->setting_bool("show_actions", true)) {
      for (const std::string& label : toast.action_labels) {
        tmpl.addAction(Utf8ToWide(label.c_str()));
      }
    }

    auto* handler = new ToastHandler(sink, toast.seq, toast.url, toast.action_ids);
    WinToast::WinToastError error = WinToast::NoError;
    const INT64 id = WinToast::instance()->showToast(tmpl, handler, &error);
    if (id >= 0) {
      last_show_error_ = -1;
      return;
    }
    const int code = static_cast<int>(error);
    if (code == last_show_error_) return;
    last_show_error_ = code;
    host()->logf(LogLevel::Warn, "无法显示 Windows 原生通知：%s。",
                 ToastErrorText(error));
  }

  void DeliverClick(const Click& click) {
    auto* task = new ClickTask{host(), click.seq, click.action_id, click.url};
    if (!host()->post_to_main_thread(&DeliverClickOnMain, task)) {
      delete task;
      host()->log(LogLevel::Debug, "原生通知点击未能投递到主线程，已丢弃。");
    }
  }

  std::thread worker_;
  std::shared_ptr<Sink> sink_;
  bool win_toast_ready_ = false;
  bool win_toast_failed_ = false;
  int last_show_error_ = -1;
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, WinToastNotifier);

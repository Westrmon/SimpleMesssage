// SimpleMessage — 插件侧看到的宿主上下文（sm::IHostContext 的唯一实现）
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "sm/host_context.h"
#include "sm/plugin.h"

#include "host/config.h"

namespace sm {
namespace host {

inline constexpr std::size_t kMaxErrorMessageBytes = 512;

inline constexpr std::size_t kArenaBytes = 4u * 1024u * 1024u;

class Arena {
 public:
  Arena();
  ~Arena();
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  void Reset();
  void* Allocate(std::size_t bytes);

  bool        Exhausted() const { return exhausted_; }
  std::size_t Used() const { return used_; }
  std::size_t Capacity() const { return capacity_; }

 private:
  char*       data_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t used_ = 0;
  bool        exhausted_ = false;
};

class CallScope {
 public:
  explicit CallScope(Arena* arena);
  ~CallScope();
  CallScope(const CallScope&) = delete;
  CallScope& operator=(const CallScope&) = delete;

  static Arena* Current();

 private:
  Arena* previous_ = nullptr;
};

struct StartCall {
  bool                             crashed = false;
  sm::StartResult                  result = sm::StartResult::Empty;
  sm::Span<const sm::Notification> items{};
  const char*                      error = nullptr;
  std::string                      error_fallback;

  const char* error_text() const {
    return (error != nullptr && error[0] != '\0') ? error : error_fallback.c_str();
  }
};

class IScheduleSink {
 public:
  virtual ~IScheduleSink() = default;

  virtual void Add(const std::string& plugin_id) = 0;

  virtual void ScheduleNext(const std::string& plugin_id, std::int64_t delay_ms) = 0;
  virtual void ScheduleAt(const std::string& plugin_id, std::int64_t unix_ms) = 0;

  virtual void Remove(const std::string& plugin_id) = 0;

  virtual bool IsCancelled(const std::string& plugin_id) const = 0;

  virtual bool IsExecuting(const std::string& plugin_id) const = 0;
};

class IPluginRunner {
 public:
  virtual ~IPluginRunner() = default;

  virtual StartCall RunStart(const std::string& plugin_id) = 0;

  virtual void DisablePlugin(const std::string& plugin_id, const char* reason,
                             bool crashed) = 0;
};

class INotificationSink {
 public:
  virtual ~INotificationSink() = default;

  virtual void OnNotifications(const std::string& source_plugin_id,
                               sm::Span<const sm::Notification> items) = 0;
};

class INotifierDelivery {
 public:
  virtual ~INotifierDelivery() = default;

  virtual bool Deliver(const std::string& notifier_id, const sm::Notification& n) = 0;
};

class IUiSession {
 public:
  virtual ~IUiSession() = default;

  virtual void* ImGuiContext() = 0;

  virtual void RequestSurface(const std::string& plugin_id, std::int64_t duration_ms) = 0;
  virtual void SetViewTitle(const std::string& plugin_id, const char* title) = 0;
  virtual void CloseView(const std::string& plugin_id) = 0;
};

// 通知图标：把 Notification::icon 变成当前渲染后端可用的纹理。
// 实现位于 ImageCache，负责异步下载/解码与缓存。
class IIconProvider {
 public:
  virtual ~IIconProvider() = default;

  virtual std::uint64_t Texture(const char* source, int* out_width, int* out_height,
                                bool* out_failed) = 0;
};

class IPluginActionSink {
 public:
  virtual ~IPluginActionSink() = default;

  virtual bool InvokeAction(const std::string& plugin_id, std::int64_t notification_seq,
                            const char* action_id) = 0;
  virtual bool OpenUrl(const char* url) = 0;
};

class IActionRunner {
 public:
  virtual ~IActionRunner() = default;
  virtual bool InvokeOnAction(const std::string& plugin_id,
                              const sm::Notification& notification,
                              const char* action_id) = 0;
};

inline constexpr std::size_t kMainThreadQueueCapacity = 256;

class MainThreadQueue {
 public:
  bool Push(const std::string& plugin_id, sm::MainThreadTaskFn fn, void* user);

  struct Item {
    std::string          plugin_id;
    sm::MainThreadTaskFn fn = nullptr;
    void*                user = nullptr;
  };

  bool Pop(Item* out);
  std::size_t Size() const;
  std::size_t Clear();

 private:
  mutable std::mutex mutex_;
  std::deque<Item>   items_;
};

struct HostEnvironment {
  const config::Paths* paths = nullptr;
  config::ConfigStore* config = nullptr;
  IScheduleSink*       scheduler = nullptr;
  IUiSession*          ui = nullptr;
  IIconProvider*       icons = nullptr;
  IPluginActionSink*   actions = nullptr;
  const std::atomic<bool>* shutting_down = nullptr;
};

struct PluginHostSetup {
  std::string              plugin_id;
  std::string              own_config_path;
  config::PluginSettings*  settings = nullptr;
  HostEnvironment          env{};
  MainThreadQueue*         post_queue = nullptr;
};

class PluginHost final : public sm::IHostContext {
 public:
  explicit PluginHost(PluginHostSetup setup);
  ~PluginHost() override;
  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  std::string LastErrorText() const;
  void        ClearError();

  const char*  plugin_id() const { return plugin_id_.c_str(); }

  const char*  host_version() const override;
  std::int32_t host_api_version() const override;
  const char*  config_dir() const override;
  const char*  data_dir() const override;
  const char*  plugin_dir() const override;
  const char*  own_config_path() const override;
  std::int64_t now_unix_ms() const override;
  const char*  locale() const override;

  void* allocate(std::size_t bytes) override;

  void schedule_next(std::int64_t delay_ms) override;
  void schedule_at(std::int64_t unix_ms) override;

  bool         setting_bool(const char* name, bool fallback) const override;
  std::int64_t setting_int(const char* name, std::int64_t fallback) const override;
  double       setting_double(const char* name, double fallback) const override;
  const char*  setting_string(const char* name, const char* fallback) const override;
  const char*  setting_enum(const char* name, const char* fallback) const override;

  void log(sm::LogLevel level, const char* message) override;
  void logf(sm::LogLevel level, const char* fmt, ...) override;

  void set_error(const char* message) override;

  bool is_cancelled() const override;

  void* imgui_context() override;
  void  request_ui_surface(std::int64_t duration_ms) override;
  void  set_view_title(const char* title) override;
  void  close_view() override;
  std::uint64_t icon_texture(const char* icon, int* out_width, int* out_height,
                             bool* out_failed) override;
  bool  invoke_action(std::int64_t notification_seq, const char* action_id) override;
  bool  open_url(const char* url) override;

  bool post_to_main_thread(sm::MainThreadTaskFn fn, void* user) override;

 private:
  void WarnMissingSink(const char* method, bool* warned);

  std::string             plugin_id_;
  std::string             own_config_path_;
  config::PluginSettings* settings_ = nullptr;
  HostEnvironment         env_{};
  MainThreadQueue*        post_queue_ = nullptr;
  mutable std::mutex      error_mutex_;
  std::string             error_;
  bool                    warned_scheduler_ = false;
  bool                    warned_ui_ = false;
};

}  // namespace host
}  // namespace sm

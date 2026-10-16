// SimpleMessage — 插件宿主上下文的实现
#include "host/plugin_host.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sm/version.h"

#include "host/log.h"
#include "host/util.h"

namespace smlog = sm::log;

namespace sm {
namespace host {

Arena::Arena() : capacity_(kArenaBytes) {}

Arena::~Arena() { std::free(data_); }

void Arena::Reset() {
  used_ = 0;
  exhausted_ = false;
}

void* Arena::Allocate(std::size_t bytes) {
  if (bytes == 0) return nullptr;

  if (data_ == nullptr) {
    data_ = static_cast<char*>(std::malloc(capacity_));
    if (data_ == nullptr) {
      exhausted_ = true;
      return nullptr;
    }
  }

  constexpr std::size_t kAlign = alignof(std::max_align_t);
  if (bytes > capacity_) {
    exhausted_ = true;
    return nullptr;
  }
  const std::size_t aligned = (bytes + (kAlign - 1)) & ~(kAlign - 1);
  if (used_ + aligned > capacity_) {
    exhausted_ = true;
    return nullptr;
  }

  char* result = data_ + used_;
  used_ += aligned;
  return result;
}

namespace {
thread_local Arena* t_call_arena = nullptr;
}  // namespace

CallScope::CallScope(Arena* arena) : previous_(t_call_arena) {
  if (arena != nullptr) arena->Reset();
  t_call_arena = arena;
}

CallScope::~CallScope() { t_call_arena = previous_; }

Arena* CallScope::Current() { return t_call_arena; }

bool MainThreadQueue::Push(const std::string& plugin_id, sm::MainThreadTaskFn fn,
                           void* user) {
  if (fn == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (items_.size() >= kMainThreadQueueCapacity) return false;
  items_.push_back(Item{plugin_id, fn, user});
  return true;
}

bool MainThreadQueue::Pop(Item* out) {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (items_.empty()) return false;
  *out = std::move(items_.front());
  items_.pop_front();
  return true;
}

std::size_t MainThreadQueue::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return items_.size();
}

std::size_t MainThreadQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::size_t dropped = items_.size();
  items_.clear();
  return dropped;
}

PluginHost::PluginHost(PluginHostSetup setup)
    : plugin_id_(std::move(setup.plugin_id)),
      own_config_path_(std::move(setup.own_config_path)),
      settings_(setup.settings),
      env_(setup.env),
      post_queue_(setup.post_queue) {}

PluginHost::~PluginHost() = default;

void PluginHost::WarnMissingSink(const char* method, bool* warned) {
  if (warned == nullptr || *warned) return;
  *warned = true;
  smlog::Writef(sm::LogLevel::Warn, plugin_id_.c_str(),
              "%s() 被调用，但宿主尚未接上对应子系统，本次调用被忽略", method);
}

const char* PluginHost::host_version() const { return SM_VERSION_STRING; }

std::int32_t PluginHost::host_api_version() const { return SM_API_VERSION; }

const char* PluginHost::config_dir() const {
  return env_.paths != nullptr ? env_.paths->config_dir.c_str() : "";
}

const char* PluginHost::data_dir() const {
  return env_.paths != nullptr ? env_.paths->data_dir.c_str() : "";
}

const char* PluginHost::plugin_dir() const {
  return env_.paths != nullptr ? env_.paths->plugin_dir.c_str() : "";
}

const char* PluginHost::own_config_path() const { return own_config_path_.c_str(); }

std::int64_t PluginHost::now_unix_ms() const { return util::now_unix_ms(); }

const char* PluginHost::locale() const { return "zh-CN"; }

void* PluginHost::allocate(std::size_t bytes) {
  if (bytes == 0) return nullptr;

  Arena* const arena = CallScope::Current();
  if (arena == nullptr) {
    smlog::Writef(sm::LogLevel::Warn, plugin_id_.c_str(),
                "allocate() 在插件回调之外被调用，返回 nullptr（arena 只在回调期间存在）");
    return nullptr;
  }

  void* const result = arena->Allocate(bytes);
  if (result == nullptr) {
    smlog::Writef(sm::LogLevel::Warn, plugin_id_.c_str(),
                "本次回调的 arena 已耗尽（已用 %zu 字节 / 上限 %zu 字节），"
                "allocate(%zu) 返回 nullptr",
                arena->Used(), arena->Capacity(), bytes);
  }
  return result;
}

void PluginHost::schedule_next(std::int64_t delay_ms) {
  if (delay_ms <= 0) {
    smlog::Writef(sm::LogLevel::Warn, plugin_id_.c_str(),
                "schedule_next(%lld) 的参数非正数，本次调用被忽略", delay_ms);
    return;
  }
  if (env_.scheduler == nullptr) {
    WarnMissingSink("schedule_next", &warned_scheduler_);
    return;
  }
  env_.scheduler->ScheduleNext(plugin_id_, delay_ms);
}

void PluginHost::schedule_at(std::int64_t unix_ms) {
  if (unix_ms <= 0) {
    smlog::Writef(sm::LogLevel::Warn, plugin_id_.c_str(),
                "schedule_at(%lld) 的参数非正数，本次调用被忽略", unix_ms);
    return;
  }
  if (env_.scheduler == nullptr) {
    WarnMissingSink("schedule_at", &warned_scheduler_);
    return;
  }
  env_.scheduler->ScheduleAt(plugin_id_, unix_ms);
}

bool PluginHost::setting_bool(const char* name, bool fallback) const {
  return settings_ != nullptr ? settings_->GetBool(name, fallback) : fallback;
}

std::int64_t PluginHost::setting_int(const char* name, std::int64_t fallback) const {
  return settings_ != nullptr ? settings_->GetInt(name, fallback) : fallback;
}

double PluginHost::setting_double(const char* name, double fallback) const {
  return settings_ != nullptr ? settings_->GetDouble(name, fallback) : fallback;
}

const char* PluginHost::setting_string(const char* name, const char* fallback) const {
  return settings_ != nullptr ? settings_->GetString(name, fallback) : fallback;
}

const char* PluginHost::setting_enum(const char* name, const char* fallback) const {
  return settings_ != nullptr ? settings_->GetString(name, fallback) : fallback;
}

void PluginHost::log(sm::LogLevel level, const char* message) {
  if (message == nullptr) return;
  smlog::Write(level, plugin_id_.c_str(), message);
}

void PluginHost::logf(sm::LogLevel level, const char* fmt, ...) {
  char buffer[smlog::kMessageCap];

  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt != nullptr ? fmt : "", args);
  va_end(args);

  smlog::Write(level, plugin_id_.c_str(), buffer);
}

std::string PluginHost::LastErrorText() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return error_;
}

void PluginHost::ClearError() {
  std::lock_guard<std::mutex> lock(error_mutex_);
  error_.clear();
}

void PluginHost::set_error(const char* message) {
  if (message == nullptr) {
    ClearError();
    return;
  }
  std::size_t length = 0;
  while (length < kMaxErrorMessageBytes && message[length] != '\0') ++length;

  std::lock_guard<std::mutex> lock(error_mutex_);
  error_.assign(message, length);
  if (error_.empty()) return;

  smlog::Write(sm::LogLevel::Warn, plugin_id_.c_str(), error_.c_str());
}

bool PluginHost::is_cancelled() const {
  if (env_.shutting_down != nullptr && env_.shutting_down->load()) return true;
  if (env_.scheduler != nullptr) return env_.scheduler->IsCancelled(plugin_id_);
  return false;
}

void* PluginHost::imgui_context() {
  return env_.ui != nullptr ? env_.ui->ImGuiContext() : nullptr;
}

void PluginHost::request_ui_surface(std::int64_t duration_ms) {
  if (env_.ui == nullptr) {
    WarnMissingSink("request_ui_surface", &warned_ui_);
    return;
  }
  env_.ui->RequestSurface(plugin_id_, duration_ms);
}

void PluginHost::set_view_title(const char* title) {
  if (env_.ui == nullptr) {
    WarnMissingSink("set_view_title", &warned_ui_);
    return;
  }
  env_.ui->SetViewTitle(plugin_id_, title);
}

void PluginHost::close_view() {
  if (env_.ui == nullptr) {
    WarnMissingSink("close_view", &warned_ui_);
    return;
  }
  env_.ui->CloseView(plugin_id_);
}

bool PluginHost::invoke_action(std::int64_t notification_seq, const char* action_id) {
  if (env_.actions == nullptr) {
    WarnMissingSink("invoke_action", &warned_ui_);
    return false;
  }
  return env_.actions->InvokeAction(plugin_id_, notification_seq, action_id);
}

bool PluginHost::open_url(const char* url) {
  if (env_.actions == nullptr) {
    WarnMissingSink("open_url", &warned_ui_);
    return false;
  }
  return env_.actions->OpenUrl(url);
}

bool PluginHost::post_to_main_thread(sm::MainThreadTaskFn fn, void* user) {
  if (fn == nullptr) return false;
  if (post_queue_ == nullptr) return false;
  if (env_.shutting_down != nullptr && env_.shutting_down->load()) return false;
  return post_queue_->Push(plugin_id_, fn, user);
}

}  // namespace host
}  // namespace sm

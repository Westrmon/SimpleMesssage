// SimpleMessage — 测试用的 IHostContext 替身
#pragma once

#include <cstddef>
#include <cstdint>

#include "sm/host_context.h"
#include "sm/version.h"

class FakeHostContext : public sm::IHostContext {
 public:
  explicit FakeHostContext(std::size_t arena_bytes = 4096)
      : arena_(new char[arena_bytes]), capacity_(arena_bytes) {}
  ~FakeHostContext() override { delete[] arena_; }

  FakeHostContext(const FakeHostContext&) = delete;
  FakeHostContext& operator=(const FakeHostContext&) = delete;

  void* allocate(std::size_t bytes) override {
    const std::size_t aligned = (bytes + 7u) & ~static_cast<std::size_t>(7u);
    if (used_ + aligned > capacity_) return nullptr;
    char* result = arena_ + used_;
    used_ += aligned;
    return result;
  }

  const char* host_version() const override { return "0.1.0"; }
  std::int32_t host_api_version() const override { return SM_API_VERSION; }
  const char* config_dir() const override { return ""; }
  const char* data_dir() const override { return ""; }
  const char* plugin_dir() const override { return ""; }
  const char* own_config_path() const override { return ""; }
  std::int64_t now_unix_ms() const override { return 0; }
  const char* locale() const override { return "zh-CN"; }

  void schedule_next(std::int64_t) override {}
  void schedule_at(std::int64_t) override {}

  bool setting_bool(const char*, bool) const override { return false; }
  std::int64_t setting_int(const char*, std::int64_t) const override { return 0; }
  double setting_double(const char*, double) const override { return 0.0; }
  const char* setting_string(const char*, const char*) const override { return ""; }
  const char* setting_enum(const char*, const char*) const override { return ""; }

  void log(sm::LogLevel, const char*) override {}
  void logf(sm::LogLevel, const char*, ...) override {}
  void set_error(const char*) override {}
  bool is_cancelled() const override { return false; }

  void* imgui_context() override { return nullptr; }
  void request_ui_surface(std::int64_t) override {}
  void set_view_title(const char*) override {}
  void close_view() override {}

  bool invoke_action(std::int64_t, const char*) override { return false; }
  bool open_url(const char*) override { return false; }
  bool post_to_main_thread(sm::MainThreadTaskFn, void*) override { return false; }

 private:
  char* arena_;
  std::size_t capacity_;
  std::size_t used_ = 0;
};

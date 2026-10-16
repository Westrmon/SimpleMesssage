// SimpleMessage 测试插件 —— 共用零件
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"
#include "sm/types.h"

namespace sm_test {

constexpr sm::PluginMeta MakeMeta(const char* id, const char* name, const char* description,
                                  sm::PluginKind kind,
                                  sm::Span<const sm::SettingDef> settings) {
  sm::PluginMeta meta{};
  meta.id = id;
  meta.name = name;
  meta.author = "SimpleMessage 测试";
  meta.version = "1.0.0";
  meta.description = description;
  meta.kind = kind;
  meta.host_range = ">=0.1.0,<1.0.0";
  meta.subscriptions = sm::Span<const char*>{};
  meta.settings = settings;
  meta.api_version = SM_API_VERSION;
  return meta;
}

constexpr sm::PluginMeta SourceMeta(const char* id, const char* name, const char* description,
                                    sm::Span<const sm::SettingDef> settings = {}) {
  return MakeMeta(id, name, description, sm::PluginKind::Source, settings);
}

constexpr sm::PluginMeta NotifierMeta(const char* id, const char* name, const char* description,
                                      sm::Span<const sm::SettingDef> settings = {}) {
  return MakeMeta(id, name, description, sm::PluginKind::Notifier, settings);
}

inline void CrashNullDereference(std::uintptr_t address = 0) {
  volatile std::uintptr_t* const target =
      reinterpret_cast<volatile std::uintptr_t*>(address);
  *target = 0x5A5A5A5A;
}

inline void SleepMs(std::int64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline bool SleepCancellable(sm::IHostContext* host, std::int64_t ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (host->is_cancelled()) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return !host->is_cancelled();
}

inline bool FillNotification(sm::IHostContext* host, sm::Notification* out,
                             const char* topic, const char* title, const char* content,
                             sm::Level level) {
  *out = sm::Notification{};
  out->topic = host->allocate_string(topic, sm::limits::kMaxTopicBytes);
  out->title = host->allocate_string(title, sm::limits::kMaxTitleBytes);
  out->content = host->allocate_string(content, sm::limits::kMaxContentBytes);
  out->level = level;
  out->time_unix_ms = host->now_unix_ms();
  return out->topic != nullptr && out->title != nullptr && out->content != nullptr;
}

inline sm::StartResult ReturnOne(sm::IHostContext* host, sm::StartOutput* out,
                                 const char* topic, const char* title, const char* content,
                                 sm::Level level = sm::Level::Info) {
  sm::Notification* const items = host->allocate_array<sm::Notification>(1);
  if (items == nullptr) {
    host->set_error("arena 分配失败（通知数组）");
    return sm::StartResult::Error;
  }
  if (!FillNotification(host, items, topic, title, content, level)) {
    host->set_error("arena 分配失败（通知字段）");
    return sm::StartResult::Error;
  }
  out->items = sm::Span<const sm::Notification>(items, 1);
  return sm::StartResult::Ok;
}

inline std::string Repeat(const std::string& unit, std::size_t total_bytes) {
  std::string out;
  if (unit.empty()) return out;
  out.reserve(total_bytes + unit.size());
  while (out.size() < total_bytes) out += unit;
  return out;
}

}  // namespace sm_test

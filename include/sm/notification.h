// SimpleMessage SDK — 通知数据模型
#pragma once

#include <cstdint>

#include "sm/types.h"

namespace sm {

// 通知级别（与 sm::LogLevel 是两套独立概念，映射见
// docs/插件开发/02-SDK接口参考.md §2）。
enum class Level : std::int32_t {
  Debug = 0,
  Info = 1,
  Warning = 2,
  Error = 3,
  Critical = 4,
};

// Action — 通知上的一个可点击项。只描述"有什么按钮"，不描述"按钮长什么样"；
// 点击是异步的，链路见 docs/01-架构总览.md §10.2。
struct Action {
  // 插件内唯一标识该动作的短字符串，UTF-8，如 "open" / "mute" / "mark_read"。
  const char* id = nullptr;

  // 显示给用户的标签，UTF-8，如 "打开" / "静音"。
  const char* label = nullptr;
};

// Notification — 一条通知
// 所有 const char* 字段都是 UTF-8、NUL 结尾，且必须位于调用方 arena 内。
struct Notification {

  // 标题。
  const char* title = nullptr;

  // 完整内容（详情）。
  const char* content = nullptr;

  // 简略内容。
  const char* summary = nullptr;

  // 主题，必填，UTF-8，点分命名的小写标识，如 "mail.inbox" / "sm.host.plugin"。
  const char* topic = nullptr;

  // 级别。
  Level level = Level::Info;

  // 时间戳，Unix 毫秒（UTC）。
  std::int64_t time_unix_ms = 0;

  // 稳定的去重键。
  const char* id = nullptr;

  // 关联的链接，可空。
  const char* url = nullptr;

  // 图标，可空。可以是本地图片路径（绝对路径，或相对 data_dir 的路径）、
  // file:// URL，或 http(s) 网络图片 URL。
  // Notifier 通过 IHostContext::icon_texture() 异步加载它；加载失败只是不显示，
  // 不影响通知本身。
  const char* icon = nullptr;

  // 可点击项。
  Span<Action> actions{};

  // 通知来源。
  const char* source_plugin = nullptr;

  // 宿主分配的单调递增序号（从 1 开始），用于 Notifier 判断"这条是不是新到的"。
  std::int64_t seq = 0;
};

}  // namespace sm

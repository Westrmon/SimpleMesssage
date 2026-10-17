// SimpleMessage — 平台层：宿主隐藏消息窗口
#pragma once

namespace sm {
namespace platform {

enum class SystemEventKind {
  kWake,             // 另一个实例请求唤起宿主
  kSessionQueryEnd,  // 系统询问能否关机/注销
  kSessionEnd,       // 系统即将关机/注销
};

struct SystemEvent {
  SystemEventKind kind = SystemEventKind::kWake;
  // 仅 kSessionEnd 有意义：true 表示会话确实要结束（而不是询问被取消）。
  bool ending = false;
};

// 回调返回 true 表示事件已被处理（不再交给平台的默认窗口过程）。
using SystemEventFn = bool (*)(void* user, const SystemEvent& event);

class IMessageWindow {
 public:
  virtual ~IMessageWindow() = default;

  virtual bool Create(SystemEventFn on_event, void* user) = 0;
  virtual void Destroy() = 0;
  virtual bool alive() const = 0;
};

IMessageWindow* NewMessageWindow();

}  // namespace platform
}  // namespace sm

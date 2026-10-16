// SimpleMessage — "正在处理通知"的递归标记
#pragma once

namespace sm {
namespace host {

inline thread_local int tls_notify_depth = 0;

class NotifyScope {
 public:
  NotifyScope() { ++tls_notify_depth; }
  ~NotifyScope() { --tls_notify_depth; }
  NotifyScope(const NotifyScope&) = delete;
  NotifyScope& operator=(const NotifyScope&) = delete;
};

inline bool InNotifyScope() { return tls_notify_depth > 0; }

}  // namespace host
}  // namespace sm

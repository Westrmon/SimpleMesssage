// SimpleMessage SDK — 宿主上下文
#pragma once

#include <cstddef>
#include <cstdint>

#include "sm/notification.h"
#include "sm/types.h"

namespace sm {

// 日志级别（与通知级别 sm::Level 是两套独立概念，宿主把日志事件转换成通知的
// 映射见 docs/02-宿主设计.md §10）。
enum class LogLevel : std::int32_t {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
};

// 主线程任务函数的签名。
// 硬规则：fn 必须尽快返回，不得阻塞、不得抛异常、不得再向宿主投递长任务。
using MainThreadTaskFn = void (*)(void* user);

// IHostContext
class IHostContext {
 public:
  virtual ~IHostContext() = default;

  // 一、宿主信息（任意线程可调用；返回的字符串在整个会话期间稳定有效）

  // 宿主版本，如 "0.1.0"。
  virtual const char* host_version() const = 0;

  // 宿主 API / ABI 版本，等于 SM_API_VERSION。
  virtual std::int32_t host_api_version() const = 0;

  // 配置目录绝对路径（UTF-8）。
  virtual const char* config_dir() const = 0;

  // 数据目录绝对路径（UTF-8）。
  virtual const char* data_dir() const = 0;

  // 插件目录绝对路径（UTF-8）。
  virtual const char* plugin_dir() const = 0;

  // 插件自身的配置文件路径（UTF-8），即 <config_dir>/plugins/<插件id>.toml。
  virtual const char* own_config_path() const = 0;

  // 当前时间，Unix 毫秒（UTC）。
  virtual std::int64_t now_unix_ms() const = 0;

  // 当前界面语言，BCP-47 形式，如 "zh-CN"。
  virtual const char* locale() const = 0;

  // 二、本次调用的 arena 分配器
  // 返回的内存绑定到当前这次插件回调；分配失败（超出 arena 上限或系统内存
  // 耗尽）返回 nullptr，调用方必须检查返回值。

  virtual void* allocate(std::size_t bytes) = 0;

  // 分配 n 个 T 的数组。
  template <typename T>
  T* allocate_array(std::size_t n) {
    if (n == 0) return nullptr;
    void* p = allocate(n * sizeof(T));
    return static_cast<T*>(p);
  }

  // 复制一个字符串到 arena，最多 max_bytes 字节。
  const char* allocate_string(const char* src, std::size_t max_bytes);

  // 相对调度：delay_ms 毫秒之后执行。非正数被忽略并记一条 Warn，不会清零已排的调度。
  virtual void schedule_next(std::int64_t delay_ms) = 0;

  // 绝对调度：不早于 unix_ms 执行。
  virtual void schedule_at(std::int64_t unix_ms) = 0;

  // 插件设置（任意线程可调用；缺失或类型不符时返回 fallback）
  virtual bool setting_bool(const char* name, bool fallback) const = 0;
  virtual std::int64_t setting_int(const char* name, std::int64_t fallback) const = 0;
  virtual double setting_double(const char* name, double fallback) const = 0;
  virtual const char* setting_string(const char* name, const char* fallback) const = 0;

  // 枚举型设置返回选项的 value 字符串（不是索引）。
  virtual const char* setting_enum(const char* name, const char* fallback) const = 0;

  virtual void log(LogLevel level, const char* message) = 0;

  // printf 风格。
  virtual void logf(LogLevel level, const char* fmt, ...) = 0;

  virtual void set_error(const char* message) = 0;

  // 取消 / 超时（仅允许在 start() 期间调用）
  // 返回 true 表示宿主希望插件尽快放弃本次工作。宿主不会强制终止卡死的插件
  // 代码（做不到，只会让进程更不稳定），只能靠插件自己检查这里。
  virtual bool is_cancelled() const = 0;

  // 当前这一帧画在哪张表面上的那个 imgui 上下文（实际类型 ImGuiContext*），
  // 无 UI 会话时返回 nullptr。
  // 硬规则：必须用宿主随附的 external/imgui 同一版本编译，且禁止缓存该指针、
  // ImDrawList* 或字体指针，也禁止把它从一个回调带到另一个回调
  // （见 docs/插件开发/01-入门.md §7）。
  virtual void* imgui_context() = 0;

  // 请求宿主为本插件维持一个存活 duration_ms 毫秒的 UI 会话。
  virtual void request_ui_surface(std::int64_t duration_ms) = 0;

  // 打开（或更新）本插件的详情窗口，并设置窗口标题（UTF-8）。
  // 宿主为每个插件单独建一扇窗口，里面只画 draw_view()。
  virtual void set_view_title(const char* title) = 0;

  // 关闭本插件的详情窗口并释放它持有的"持续"UI 引用。
  virtual void close_view() = 0;

  // 取通知图标的纹理（ImTextureID），供 ImGui::Image() 直接使用。
  // icon 为 Notification::icon：本地图片路径、file:// URL 或 http(s) URL。
  // 加载是异步的 —— 第一帧返回 0 并开始下载/解码，下一帧再取；
  // 就绪后返回非 0 的纹理句柄，并通过 out_width / out_height 写入像素尺寸。
  // 加载失败时返回 0 并把 *out_failed 置为 true（结果被缓存，不会反复重试）。
  // 只能在 draw_ui / draw_view 期间调用；空指针或空字符串返回 0。
  virtual std::uint64_t icon_texture(const char* icon, int* out_width,
                                     int* out_height, bool* out_failed) = 0;

  // 请求宿主在本帧绘制结束之后回调本插件的 on_action()。
  // 用于"插件自己画的界面里点击了 Action"的场景。
  // notification_seq : 目标通知的 Notification::seq（来自 on_notify /draw_ui 收到的通知）。
  // 返回 false 表示该通知已不在内存历史里（例如已被新的通知挤掉），或主线程队列已满 —— 本次点击应当被安静地忽略。
  virtual bool invoke_action(std::int64_t notification_seq, const char* action_id) = 0;

  // 用系统默认程序打开一个链接。
  // 安全约定：宿主只接受 http / https / mailto 三种协议，其余一律拒绝并记一条 Warn 日志。
  // 返回 false 表示协议被拒绝或系统调用失败。
  virtual bool open_url(const char* url) = 0;

  // 九、主线程任务投递（任意线程可调用）
  // 把 fn(user) 排进主线程队列，由主线程在下一帧调用。
  virtual bool post_to_main_thread(MainThreadTaskFn fn, void* user) = 0;
};

// allocate_string 的 inline 实现（刻意放在头文件里）。
inline const char* IHostContext::allocate_string(const char* src, std::size_t max_bytes) {
  if (src == nullptr) return nullptr;
  std::size_t len = 0;
  while (src[len] != '\0') ++len;
  if (len > max_bytes) len = max_bytes;
  char* dst = static_cast<char*>(allocate(len + 1));
  if (dst == nullptr) return nullptr;
  for (std::size_t i = 0; i < len; ++i) dst[i] = src[i];
  dst[len] = '\0';
  return dst;
}

}  // namespace sm

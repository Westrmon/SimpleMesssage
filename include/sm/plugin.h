// SimpleMessage SDK — 插件基类、元信息与设置 schema
#pragma once

#include <cstddef>
#include <cstdint>

#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/types.h"
#include "sm/version.h"

namespace sm {

// 插件类别
enum class PluginKind : std::int32_t {
  Source = 0,
  Notifier = 1,
};

// start() 的返回值。关键规则：Error 不等于"禁用"
//（退避规则见 docs/01-架构总览.md §7.4）。
enum class StartResult : std::int32_t {
  // 本次成功且有数据。
  Ok = 0,

  // 本次成功但没有新数据。
  Empty = 1,

  // 本次失败（网络不可达、文件被占用……）。
  Error = 2,

  // 插件自报"我处于不可恢复的错误状态"。
  Abort = 3,

  // 宿主取消了本次执行（看门狗超时，或宿主正在退出）。
  Cancelled = 4,
};

// 设置项 schema 的类型（完整规则见 插件开发/03-配置格式.md）。
enum class SettingType : std::int32_t {
  Bool = 0,
  Int = 1,
  Double = 2,
  String = 3,
  Enum = 4,
  // 一段声音：WAV / MP3 文件路径（存法同 String）。
  // 控制面板会在文本框旁额外给出"浏览…"与"试听"按钮。
  Sound = 5,
};

// 枚举型设置的一个选项。
struct SettingOption {
  const char* value = nullptr;
  const char* label = nullptr;
};

struct SettingDef {
  // TOML 中的键名。
  const char* name = nullptr;

  // 界面显示名（UTF-8）。
  const char* label = nullptr;

  // 设置项说明（UTF-8）。
  const char* description = nullptr;

  SettingType type = SettingType::Bool;

  // 为 true 时，控制面板会在该设置项后标注"（重启后生效）"。
  bool requires_restart = false;

  bool default_bool = false;
  std::int64_t default_int = 0;
  double default_double = 0.0;
  const char* default_string = nullptr;

  std::int64_t min_int = 0;
  std::int64_t max_int = 0;
  double min_double = 0.0;
  double max_double = 0.0;
  std::size_t max_string_bytes = 0;

  // 仅 Enum 使用。
  Span<const SettingOption> options{};
};

// 设置项的构造函数，让 schema 声明保持紧凑
//（用法与完整示例见 docs/插件开发/02-SDK接口参考.md §7）。
namespace setting {

constexpr SettingDef boolean(const char* name, bool def, const char* label,
                            const char* description = nullptr,
                            bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::Bool;
  d.requires_restart = requires_restart;
  d.default_bool = def;
  return d;
}

constexpr SettingDef integer(const char* name, std::int64_t def, std::int64_t min,
                            std::int64_t max, const char* label,
                            const char* description = nullptr,
                            bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::Int;
  d.requires_restart = requires_restart;
  d.default_int = def;
  d.min_int = min;
  d.max_int = max;
  return d;
}

constexpr SettingDef number(const char* name, double def, double min, double max,
                           const char* label, const char* description = nullptr,
                           bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::Double;
  d.requires_restart = requires_restart;
  d.default_double = def;
  d.min_double = min;
  d.max_double = max;
  return d;
}

constexpr SettingDef text(const char* name, const char* def, std::size_t max_bytes,
                         const char* label, const char* description = nullptr,
                         bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::String;
  d.requires_restart = requires_restart;
  d.default_string = def;
  d.max_string_bytes = max_bytes;
  return d;
}

constexpr SettingDef enumeration(const char* name, const char* def,
                                Span<const SettingOption> options, const char* label,
                                const char* description = nullptr,
                                bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::Enum;
  d.requires_restart = requires_restart;
  d.default_string = def;
  d.options = options;
  return d;
}

// 声音设置：值是 WAV / MP3 文件路径。
// 表单里除了文本框还会显示"浏览…"与"试听"。
constexpr SettingDef sound(const char* name, const char* def, std::size_t max_bytes,
                          const char* label, const char* description = nullptr,
                          bool requires_restart = false) {
  SettingDef d{};
  d.name = name;
  d.label = label;
  d.description = description;
  d.type = SettingType::Sound;
  d.requires_restart = requires_restart;
  d.default_string = def;
  d.max_string_bytes = max_bytes;
  return d;
}

}  // namespace setting

// 插件元信息。
// 硬规则：实例必须有静态存储期（推荐 constexpr），且在 DLL 被加载的整个期间保持不变。
struct PluginMeta {
  // 全局唯一标识。
  const char* id = nullptr;

  // 显示名（UTF-8）。
  const char* name = nullptr;

  // 作者（UTF-8）。
  const char* author = nullptr;

  // 插件自身版本，语义化版本，如 "1.2.0"。
  const char* version = nullptr;

  // 描述（UTF-8）。
  const char* description = nullptr;

  // 插件类别。
  PluginKind kind = PluginKind::Source;

  // 要求的宿主版本范围，如 ">=0.1.0,<1.0.0"。
  const char* host_range = nullptr;

  // 订阅的 topic 模式（仅 Notifier 有意义）。
  Span<const char*> subscriptions{};

  // 设置项 schema。
  Span<const SettingDef> settings{};

  // 编译时使用的 SDK API 版本，必须写 SM_API_VERSION。
  std::int32_t api_version = 0;
};

// start() 的输出
struct StartOutput {
  // 本次产生的通知数组，必须在 IHostContext 的 arena 里（见 notification.h）。
  Span<const Notification> items{};

  // 失败原因（UTF-8，arena 内）。
  const char* error = nullptr;
};

// IPlugin — 两类插件共同的基类
// 线程约定：每个方法下方的标注是硬约束；在错误的线程调用是未定义行为。
// 异常与崩溃：宿主对每一次跨边界的插件调用都包了 SEH 屏障。
class IPlugin {
 public:
  // 虚析构函数是必需的（宿主经基类指针销毁插件实例）。
  virtual ~IPlugin() = default;

  // 元信息。
  virtual const PluginMeta& meta() const = 0;

  // 初始化（加载时调用一次）。返回 false 表示初始化失败。
  virtual bool initialize() = 0;

  // 一次调度执行（仅 Source 有意义）。
  // 重要：宿主在每次调用前会清空该插件的下次调度时刻。
  // 硬规则：本方法禁止阻塞式等待，禁止做没有超时的网络/磁盘 IO。
  virtual StartResult start(StartOutput* out) = 0;

  // 停止。
  virtual void stop() = 0;

  // 退出。
  // 硬规则：本方法返回之前，插件必须销毁它创建的一切（包括 join 掉所有线程）；
  // 这里允许使用 IHostContext，但宿主可能在它返回后立即销毁上下文。
  virtual void exit() = 0;

  // Source 插件可以忽略它们（继承 PluginBase 即可，全部为空实现）。

  // 收到一条通知（硬规则：禁止阻塞）。
  // 想弹出浮层就在这里调用 IHostContext::request_ui_surface(5000)，之后宿主会在
  // 浮层存活期间每帧调用 draw_ui()。n 的生存期仅限本次调用，需要留存请拷贝。
  virtual void on_notify(const Notification& n) = 0;

  // 用户点击了某条通知上的某个 Action（硬规则：禁止阻塞、禁止同步 IO）。
  // n 的生存期仅限本次调用；action_id 是 notification.h 里声明的 id。
  virtual void on_action(const Notification& n, const char* action_id) = 0;

  // 绘制本插件的常驻界面。
  // 用 IHostContext::imgui_context() 拿到上下文，创建自己的 imgui 窗口。
  // history 是宿主内存历史的只读视图（最近在前，已按本插件的订阅过滤），生存期仅限本次调用。
  virtual void draw_ui(Span<const Notification> history) = 0;

  // 绘制本插件的详情视图。宿主为它单开一扇窗口，每帧调用本方法；
  // 这个窗口里只有它，不回落到控制面板。
  virtual void draw_view() = 0;
};

// PluginBase — 可选基类，为所有方法提供安全的空实现（最小插件骨架见
// docs/插件开发/02-SDK接口参考.md §8）。元信息要显式传两遍：构造函数收一份供实例
// 使用，而 sm_plugin_meta() 是静态导出函数，没有实例时也要能返回元信息。
// 它不持有任何资源，因此不覆盖生命周期方法也是安全的。
class PluginBase : public IPlugin {
 public:
  PluginBase(IHostContext* host, const PluginMeta& meta) : m_host(host), m_meta(&meta) {}

  const PluginMeta& meta() const override { return *m_meta; }

  bool initialize() override { return true; }

  StartResult start(StartOutput* out) override {
    out->items = Span<const Notification>{};
    out->error = nullptr;
    return StartResult::Empty;
  }

  void stop() override {}
  void exit() override {}
  void on_notify(const Notification&) override {}
  void on_action(const Notification&, const char*) override {}
  void draw_ui(Span<const Notification>) override {}
  void draw_view() override {}

 protected:
  // 宿主上下文。
  IHostContext* host() const { return m_host; }
  const PluginMeta& plugin_meta() const { return *m_meta; }

 private:
  IHostContext* m_host = nullptr;
  const PluginMeta* m_meta = nullptr;
};

}  // namespace sm

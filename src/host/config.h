// SimpleMessage — 配置子系统
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "sm/host_context.h"
#include "sm/plugin.h"
#include "sm/types.h"

namespace sm {
namespace config {

struct Paths {
  std::string config_dir;
  std::string data_dir;
  std::string plugin_dir;
  std::string config_file;
  std::string rules_file;
  std::string plugin_config_dir;
  std::string log_dir;
  std::string host_dir;
};

struct PathOverrides {
  const char* config_dir = nullptr;
  const char* data_dir = nullptr;
  const char* plugin_dir = nullptr;
};

Paths resolve_paths(const PathOverrides& overrides);

struct Diagnostic {
  enum class Severity : std::int32_t { Info = 0, Warn = 1, Error = 2 };

  Severity    severity = Severity::Info;
  std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

void AddInfo(Diagnostics* out, std::string message);
void AddWarn(Diagnostics* out, std::string message);
void AddError(Diagnostics* out, std::string message);

enum class NotifyLogLevel : std::int32_t {
  Off = 0,
  Error = 1,
  Warn = 2,
  Info = 3,
};

struct AppConfig {
  NotifyLogLevel notify_log_level = NotifyLogLevel::Error;
  sm::LogLevel   log_level = sm::LogLevel::Info;
  std::int32_t   log_retention_days = 7;
  std::int32_t   log_max_total_mb = 32;
  std::int32_t   ui_font_size = 16;
  bool           single_instance = true;
  std::int32_t   notification_history_size = 200;
  // 规则保存的全局许可；默认关闭，开启后无视通知来源。
  bool           allow_rule_save = false;
};

enum class NotifyCorner : std::int32_t {
  kTopLeft = 0,
  kTopRight = 1,
  kBottomLeft = 2,
  kBottomRight = 3,
};

struct NotifyWindowConfig {
  std::string  monitor = "primary";
  NotifyCorner corner = NotifyCorner::kBottomRight;
};

using PluginEnabledMap = std::map<std::string, bool>;
using RoutingMap = std::map<std::string, std::vector<std::string>>;

// 一条 `[[instances]]` 记录。id 由宿主生成且不可改；plugin 是插件类型 id；
// name 为空表示使用自动显示名；enabled 是用户的启用/禁用选择。
struct InstanceConfig {
  std::string id;
  std::string plugin;
  std::string name;
  bool        enabled = true;
};

struct Config {
  AppConfig          app;
  NotifyWindowConfig notify_window;
  // 每个插件的运行实例。Notifier 每种类型仍只有一个 canonical 实例。
  std::vector<InstanceConfig> instances;
  // 被用户删空的类型。存在墓碑时不再自动补 canonical 实例。
  std::vector<std::string> removed_types;
  // rules.toml 里每条规则的启用覆盖：表里没有的 id 默认启用。
  PluginEnabledMap rules;
  RoutingMap       routing;
};

class ConfigStore;

class PluginSettings {
 public:
  PluginSettings();
  ~PluginSettings();
  PluginSettings(const PluginSettings&) = delete;
  PluginSettings& operator=(const PluginSettings&) = delete;
  PluginSettings(PluginSettings&&) noexcept;
  PluginSettings& operator=(PluginSettings&&) noexcept;

  void Load(const std::string& plugin_id, const std::string& file_path,
            sm::Span<const sm::SettingDef> schema, Diagnostics* out);

  bool         GetBool(const char* name, bool fallback) const;
  std::int64_t GetInt(const char* name, std::int64_t fallback) const;
  double       GetDouble(const char* name, double fallback) const;
  // 返回副本：设置值可能被 UI 线程随时改写，返回内部 c_str() 会悬垂。
  std::string  GetStringCopy(const char* name, const char* fallback) const;

  bool SetBool(const char* name, bool value);
  bool SetInt(const char* name, std::int64_t value);
  bool SetDouble(const char* name, double value);
  bool SetString(const char* name, const std::string& value);

  void ResetToDefaults();

  sm::Span<const sm::SettingDef> Schema() const;

  bool schema_too_new() const { return schema_too_new_; }
  bool dirty() const { return dirty_; }
  bool loaded() const { return loaded_; }

 private:
  friend class ConfigStore;

  struct Value {
    sm::SettingType type = sm::SettingType::Bool;
    bool            b = false;
    std::int64_t    i = 0;
    double          d = 0.0;
    std::string     s;
  };

  struct Extra;

  void FillDefaults();

  ConfigStore* owner_ = nullptr;

  // 插件在调度线程 start() 里读设置，UI 线程同时可能写设置。
  // 递归锁：Load()/ResetToDefaults() 会调用 FillDefaults()。
  mutable std::recursive_mutex mutex_;

  const sm::SettingDef*  schema_ = nullptr;
  std::size_t            schema_size_ = 0;
  std::vector<Value>     values_;
  std::map<std::string, std::size_t> index_;
  std::string            file_path_;
  std::unique_ptr<Extra> extra_;
  bool                   loaded_ = false;
  bool                   dirty_ = false;
  bool                   schema_too_new_ = false;
};

class ConfigStore {
 public:
  // config.toml 的结构版本。
  static constexpr std::int32_t kSchemaVersion = 2;
  // config/plugins/<id>.toml 的结构版本。它与 config.toml 独立演进 ——
  // 两者共用同一个常量会让配置升级把所有既有设置文件误判为旧版本。
  static constexpr std::int32_t kSettingsSchemaVersion = 1;

  ConfigStore();
  ~ConfigStore();
  ConfigStore(const ConfigStore&) = delete;
  ConfigStore& operator=(const ConfigStore&) = delete;

  enum class LoadResult : std::int32_t {
    Ok = 0,
    SchemaTooNew = 1,
  };

  LoadResult Load(const Paths& paths, bool read_only, Diagnostics* out);

  const Paths&     PathsRef() const { return paths_; }
  const Config&    Data() const { return config_; }
  const AppConfig& App() const { return config_.app; }
  bool             read_only() const { return read_only_; }

  bool SchemaTooNewDetected() const { return schema_too_new_; }

  bool config_was_reset() const { return config_reset_; }

  const std::vector<InstanceConfig>& Instances() const { return config_.instances; }
  const InstanceConfig* FindInstance(const std::string& instance_id) const;
  std::size_t CountInstancesOfType(const std::string& type_id) const;

  bool AddInstance(InstanceConfig instance, Diagnostics* out);
  bool RemoveInstance(const std::string& instance_id, Diagnostics* out);
  bool SetInstanceEnabled(const std::string& instance_id, bool enabled);
  bool SetInstanceName(const std::string& instance_id, const std::string& name);

  bool IsTypeRemoved(const std::string& type_id) const;
  void MarkTypeRemoved(const std::string& type_id);
  void ClearTypeRemoved(const std::string& type_id);

  // 只丢弃内存里的设置对象；调用前必须确保插件的 PluginHost 已销毁。
  void ForgetPluginSettings(const std::string& instance_id);
  // 删除 <instance_id>.toml 与 .bak。失败只记诊断，不重建文件。
  bool DeletePluginSettingsFiles(const std::string& instance_id, Diagnostics* out);

  void SetRuleEnabled(const std::string& rule_id, bool enabled);
  void SetRouting(const std::string& plugin_id, std::vector<std::string> patterns);
  void SetLogLevel(sm::LogLevel level);
  void SetNotifyLogLevel(NotifyLogLevel level);
  void SetUiFontSize(std::int32_t size);
  void SetNotificationHistorySize(std::int32_t size);
  void SetNotifyWindow(const std::string& monitor, NotifyCorner corner);
  void SetAllowRuleSave(bool allowed);

  PluginSettings& EnsurePluginSettings(const std::string& plugin_id,
                                       sm::Span<const sm::SettingDef> schema,
                                       Diagnostics* out);
  bool HasPluginSettings(const std::string& plugin_id) const;
  PluginSettings& PluginSettingsFor(const std::string& plugin_id);
  void ResetPluginSettings(const std::string& plugin_id, Diagnostics* out);

  void MarkDirty();
  void Tick(std::int64_t now_ms);
  void FlushNow();
  void DrainDiagnostics(Diagnostics* out);

  bool          has_pending_write() const;
  bool          last_write_failed() const { return last_write_failed_; }
  std::int64_t  next_write_due_ms() const;

 private:
  friend class PluginSettings;

  void ArmWriteTimer();

  void Advance(std::int64_t now_ms, bool force);
  void WritePending(std::int64_t now_ms);
  bool WriteConfigFile();
  bool WritePluginFile(PluginSettings& settings);
  void Note(std::string message);
  void NoteError(std::string message);

  static constexpr std::int64_t kDebounceMs = 500;
  static constexpr std::int64_t kMaxDebounceMs = 2000;
  static constexpr std::int32_t kMaxRetries = 3;
  static constexpr std::size_t  kMaxPendingDiagnostics = 64;

  struct Extra;

  Paths     paths_;
  Config    config_;
  bool      read_only_ = false;
  bool      schema_too_new_ = false;
  bool      config_reset_ = false;

  std::unique_ptr<Extra> extra_;
  std::map<std::string, PluginSettings> plugin_settings_;

  Diagnostics   pending_diagnostics_;
  bool          dirty_ = false;
  std::int64_t  first_dirty_ms_ = 0;
  std::int64_t  last_dirty_ms_ = 0;
  std::int64_t  retry_at_ms_ = 0;
  std::int32_t  retry_count_ = 0;
  bool          retry_exhausted_ = false;
  bool          last_write_failed_ = false;
};

}  // namespace config
}  // namespace sm

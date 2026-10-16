// SimpleMessage — 配置子系统
#pragma once

#include <cstdint>
#include <map>
#include <memory>
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

struct Config {
  AppConfig          app;
  NotifyWindowConfig notify_window;
  PluginEnabledMap   plugins;
  RoutingMap         routing;
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
  const char*  GetString(const char* name, const char* fallback) const;

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
  static constexpr std::int32_t kSchemaVersion = 1;

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

  void SetPluginEnabled(const std::string& plugin_id, bool enabled);
  void SetRouting(const std::string& plugin_id, std::vector<std::string> patterns);
  void SetLogLevel(sm::LogLevel level);
  void SetNotifyLogLevel(NotifyLogLevel level);
  void SetUiFontSize(std::int32_t size);
  void SetNotificationHistorySize(std::int32_t size);
  void SetNotifyWindow(const std::string& monitor, NotifyCorner corner);

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

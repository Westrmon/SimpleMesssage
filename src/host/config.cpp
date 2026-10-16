// SimpleMessage — 配置子系统实现

#include "host/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <toml.hpp>

#include "host/util.h"
#include "platform/platform.h"

#ifndef SM_DEFAULT_PLUGIN_DIR
#define SM_DEFAULT_PLUGIN_DIR "plugins"
#endif
#ifndef SM_DEFAULT_CONFIG_FILE
#define SM_DEFAULT_CONFIG_FILE "config.toml"
#endif

namespace sm {
namespace config {
namespace {

bool ParseToml(const std::string& text, const std::string& source_path, toml::table* out,
               std::string* error) {
  try {
    *out = toml::parse(text, std::string_view(source_path));
    return true;
  } catch (const toml::parse_error& err) {
    *error = std::string(err.description());
    return false;
  } catch (const std::exception& err) {
    *error = err.what();
    return false;
  }
}

std::string CorruptStamp() {
  const std::time_t now = std::time(nullptr);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &now);
#else
  localtime_r(&now, &parts);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
  return text;
}

void RenameCorrupt(const std::string& path) {
  util::rename_file(path, path + ".corrupt-" + CorruptStamp());
}

bool TruncateUtf8(std::string* text, std::size_t max_bytes) {
  if (text->size() <= max_bytes) return false;
  std::size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>((*text)[cut]) & 0xC0u) == 0x80u) --cut;
  text->resize(cut);
  return true;
}

struct Reader {
  const char*  file = nullptr;
  Diagnostics* out = nullptr;
  bool         corrected = false;

  void Report(Diagnostic::Severity severity, std::string message) {
    if (out == nullptr) return;
    std::string text = "[";
    text += (file != nullptr) ? file : "<配置>";
    text += "] ";
    text += message;
    out->push_back(Diagnostic{severity, std::move(text)});
  }

  void Info(std::string message) { Report(Diagnostic::Severity::Info, std::move(message)); }
  void Warn(std::string message) { Report(Diagnostic::Severity::Warn, std::move(message)); }
  void Error(std::string message) { Report(Diagnostic::Severity::Error, std::move(message)); }
};

std::string SettingName(const sm::SettingDef& def) {
  return (def.name != nullptr) ? def.name : std::string("<未命名>");
}

void WarnUnknownKeys(const toml::table* table, const char* section,
                     std::initializer_list<const char*> known, Reader* r) {
  if (table == nullptr || r == nullptr) return;
  for (auto&& entry : *table) {
    const std::string key(entry.first.str());
    bool recognized = false;
    for (const char* candidate : known) {
      if (key == candidate) {
        recognized = true;
        break;
      }
    }
    if (!recognized) {
      r->Warn(std::string("[") + section + "] 中有未知的键 " + key +
              "，本次忽略（保留在文件中）");
    }
  }
}

std::string NumberText(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", value);
  return std::string(buffer);
}

void ReadBool(const toml::node& node, const sm::SettingDef& def, Reader& r, bool* out) {
  const auto* value = node.as_boolean();
  if (value == nullptr) {
    r.Warn("设置项 " + SettingName(def) + " 不是布尔值，改用默认值");
    *out = def.default_bool;
    r.corrected = true;
    return;
  }
  *out = value->get();
}

void ReadInt(const toml::node& node, const sm::SettingDef& def, Reader& r,
             std::int64_t* out) {
  const auto* value = node.as_integer();
  if (value == nullptr) {
    r.Warn("设置项 " + SettingName(def) + " 不是整数，改用默认值");
    *out = def.default_int;
    r.corrected = true;
    return;
  }

  std::int64_t number = value->get();
  if (def.min_int != 0 || def.max_int != 0) {
    if (number < def.min_int || number > def.max_int) {
      const std::int64_t clamped = (number < def.min_int) ? def.min_int : def.max_int;
      r.Warn("设置项 " + SettingName(def) + " = " + std::to_string(number) +
             " 超出范围 [" + std::to_string(def.min_int) + ", " +
             std::to_string(def.max_int) + "]，夹到 " + std::to_string(clamped));
      number = clamped;
      r.corrected = true;
    }
  }
  *out = number;
}

void ReadDouble(const toml::node& node, const sm::SettingDef& def, Reader& r, double* out) {
  double number = 0.0;
  if (const auto* value = node.as_floating_point()) {
    number = value->get();
  } else if (const auto* integer = node.as_integer()) {
    number = static_cast<double>(integer->get());
  } else {
    r.Warn("设置项 " + SettingName(def) + " 不是数值，改用默认值");
    *out = def.default_double;
    r.corrected = true;
    return;
  }

  if (def.min_double != 0.0 || def.max_double != 0.0) {
    if (number < def.min_double || number > def.max_double) {
      const double clamped = (number < def.min_double) ? def.min_double : def.max_double;
      r.Warn("设置项 " + SettingName(def) + " = " + NumberText(number) +
             " 超出范围 [" + NumberText(def.min_double) + ", " +
             NumberText(def.max_double) + "]，夹到 " + NumberText(clamped));
      number = clamped;
      r.corrected = true;
    }
  }
  *out = number;
}

void ReadString(const toml::node& node, const sm::SettingDef& def, Reader& r,
                std::string* out) {
  const auto* value = node.as_string();
  if (value == nullptr) {
    r.Warn("设置项 " + SettingName(def) + " 不是字符串，改用默认值");
    *out = (def.default_string != nullptr) ? def.default_string : "";
    r.corrected = true;
    return;
  }

  std::string text = value->get();
  const std::size_t cap =
      (def.max_string_bytes != 0) ? def.max_string_bytes : sm::limits::kMaxStringSettingBytes;
  if (TruncateUtf8(&text, cap)) {
    r.Warn("设置项 " + SettingName(def) + " 超过 " + std::to_string(cap) + " 字节，已截断");
    r.corrected = true;
  }
  *out = std::move(text);
}

void ReadEnum(const toml::node& node, const sm::SettingDef& def, Reader& r,
              std::string* out) {
  const std::string fallback = (def.default_string != nullptr) ? def.default_string : "";
  const auto* value = node.as_string();
  if (value == nullptr) {
    r.Warn("设置项 " + SettingName(def) + " 不是字符串，改用默认值 " + fallback);
    *out = fallback;
    r.corrected = true;
    return;
  }

  const std::string text = value->get();
  for (std::size_t i = 0; i < def.options.size; ++i) {
    const sm::SettingOption& option = def.options[i];
    if (option.value != nullptr && text == option.value) {
      *out = text;
      return;
    }
  }
  r.Warn("设置项 " + SettingName(def) + " 的值 \"" + text + "\" 不在选项列表里，改用默认值 " +
         fallback);
  *out = fallback;
  r.corrected = true;
}

using MigrateFn = bool (*)(toml::table& table, Reader& r);

MigrateFn MigrationStep(std::int32_t from) {
  (void)from;
  return nullptr;
}

bool RunMigrations(toml::table* table, std::int32_t from, std::int32_t target, Reader& r) {
  while (from < target) {
    const MigrateFn step = MigrationStep(from);
    if (step == nullptr || !step(*table, r)) return false;
    ++from;
  }
  return true;
}

const char* NotifyLevelName(NotifyLogLevel level) {
  switch (level) {
    case NotifyLogLevel::Off:   return "off";
    case NotifyLogLevel::Error: return "error";
    case NotifyLogLevel::Warn:  return "warn";
    case NotifyLogLevel::Info:  return "info";
  }
  return "error";
}

const char* CornerName(NotifyCorner corner) {
  switch (corner) {
    case NotifyCorner::kTopLeft:     return "top_left";
    case NotifyCorner::kTopRight:    return "top_right";
    case NotifyCorner::kBottomLeft:  return "bottom_left";
    case NotifyCorner::kBottomRight: return "bottom_right";
  }
  return "bottom_right";
}

const char* LogLevelName(sm::LogLevel level) {
  switch (level) {
    case sm::LogLevel::Trace: return "trace";
    case sm::LogLevel::Debug: return "debug";
    case sm::LogLevel::Info:  return "info";
    case sm::LogLevel::Warn:  return "warn";
    case sm::LogLevel::Error: return "error";
  }
  return "info";
}

std::string NormalizeDir(const char* given, const std::string& exe_dir) {
  std::string path = (given != nullptr) ? given : "";
  if (path.empty() || util::path_is_absolute(path) || exe_dir.empty()) return path;
  return util::path_join(exe_dir, path);
}

bool DirectoryWritable(const std::string& dir) {
  if (dir.empty() || !util::ensure_directory(dir)) return false;
  return util::is_directory_writable(dir);
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#if defined(_WIN32)
std::string OsFallbackRoot() {
  const char* base = std::getenv("APPDATA");
  if (base == nullptr || *base == 0) return {};
  return util::path_join(base, "SimpleMessage");
}
#else
std::string OsFallbackRoot() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg != nullptr && *xdg != 0) return util::path_join(xdg, "simplemessage");
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == 0) return {};
  return util::path_join(util::path_join(home, ".config"), "simplemessage");
}
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace

struct ConfigStore::Extra {
  toml::table source;
};

struct PluginSettings::Extra {
  toml::table source;
};

ConfigStore::ConfigStore() = default;
ConfigStore::~ConfigStore() = default;

PluginSettings::PluginSettings() = default;
PluginSettings::~PluginSettings() = default;
PluginSettings::PluginSettings(PluginSettings&&) noexcept = default;
PluginSettings& PluginSettings::operator=(PluginSettings&&) noexcept = default;

void AddInfo(Diagnostics* out, std::string message) {
  if (out == nullptr) return;
  out->push_back(Diagnostic{Diagnostic::Severity::Info, std::move(message)});
}

void AddWarn(Diagnostics* out, std::string message) {
  if (out == nullptr) return;
  out->push_back(Diagnostic{Diagnostic::Severity::Warn, std::move(message)});
}

void AddError(Diagnostics* out, std::string message) {
  if (out == nullptr) return;
  out->push_back(Diagnostic{Diagnostic::Severity::Error, std::move(message)});
}

Paths resolve_paths(const PathOverrides& overrides) {
  Paths paths;
  const std::string exe_dir = util::exe_directory();

  const std::string requested_config = NormalizeDir(overrides.config_dir, exe_dir);
  const std::string beside_exe =
      exe_dir.empty() ? std::string{} : util::path_join(exe_dir, "config");

  if (!requested_config.empty()) {
    paths.config_dir = requested_config;
    util::ensure_directory(paths.config_dir);
  } else if (DirectoryWritable(beside_exe)) {
    paths.config_dir = beside_exe;
  } else {
    const std::string root = OsFallbackRoot();
    std::string fallback = root.empty() ? std::string() : util::path_join(root, "config");
    if (fallback.empty()) fallback = beside_exe.empty() ? std::string("config") : beside_exe;
    paths.config_dir = fallback;
    util::ensure_directory(paths.config_dir);
  }

  const std::string requested_data = NormalizeDir(overrides.data_dir, exe_dir);
  if (!requested_data.empty()) {
    paths.data_dir = requested_data;
  } else {
    const std::string parent = util::path_dir(paths.config_dir);
    if (!parent.empty()) {
      paths.data_dir = util::path_join(parent, "data");
    } else {
      const std::string root = OsFallbackRoot();
      paths.data_dir = root.empty() ? std::string("data") : util::path_join(root, "data");
    }
  }
  util::ensure_directory(paths.data_dir);

  const std::string requested_plugin = NormalizeDir(overrides.plugin_dir, exe_dir);
  if (!requested_plugin.empty()) {
    paths.plugin_dir = requested_plugin;
  } else if (!exe_dir.empty()) {
    paths.plugin_dir = util::path_join(exe_dir, SM_DEFAULT_PLUGIN_DIR);
  } else {
    paths.plugin_dir = SM_DEFAULT_PLUGIN_DIR;
  }

  paths.config_file = util::path_join(paths.config_dir, SM_DEFAULT_CONFIG_FILE);
  paths.plugin_config_dir = util::path_join(paths.config_dir, "plugins");
  paths.log_dir = util::path_join(paths.data_dir, "logs");
  paths.host_dir = util::path_join(paths.data_dir, "host");
  util::ensure_directory(paths.host_dir);
  return paths;
}

void PluginSettings::FillDefaults() {
  values_.assign(schema_size_, Value{});
  for (std::size_t i = 0; i < schema_size_; ++i) {
    const sm::SettingDef& def = schema_[i];
    Value& value = values_[i];
    value.type = def.type;
    value.b = def.default_bool;
    value.i = def.default_int;
    value.d = def.default_double;
    value.s = (def.default_string != nullptr) ? def.default_string : "";
  }
}

void PluginSettings::Load(const std::string& plugin_id, const std::string& file_path,
                          sm::Span<const sm::SettingDef> schema, Diagnostics* out) {
  schema_ = schema.data;
  schema_size_ = schema.size;
  file_path_ = file_path;
  extra_.reset();
  index_.clear();
  for (std::size_t i = 0; i < schema_size_; ++i) {
    if (schema_[i].name != nullptr) index_.emplace(schema_[i].name, i);
  }
  FillDefaults();
  loaded_ = true;
  dirty_ = false;
  schema_too_new_ = false;

  Reader r;
  r.file = file_path.c_str();
  r.out = out;

  std::string text;
  if (!util::file_exists(file_path)) {
    r.Info("插件 " + plugin_id + " 的设置文件不存在，按默认值生成");
    dirty_ = true;
  } else if (!util::read_text_file(file_path, &text)) {
    r.Warn("插件 " + plugin_id + " 的设置文件无法读取，本次按默认值运行");
    dirty_ = true;
  } else {
    toml::table parsed;
    std::string failure;
    if (!ParseToml(text, file_path, &parsed, &failure)) {
      RenameCorrupt(file_path);
      r.Error("插件 " + plugin_id + " 的设置文件解析失败：" + failure +
              "，原文件已改名为 " + util::path_filename(file_path) + ".corrupt-*，按默认值重新生成");
      dirty_ = true;
    } else {
      extra_.reset(new Extra{std::move(parsed)});
      const toml::table& root = extra_->source;

      std::int32_t version = ConfigStore::kSchemaVersion;
      if (const toml::node* node = root.get("schema_version")) {
        if (const auto* integer = node->as_integer()) {
          version = static_cast<std::int32_t>(integer->get());
        } else {
          r.Warn("schema_version 不是整数，按当前版本处理");
          r.corrected = true;
        }
      } else {
        r.Warn("缺少 schema_version，按当前版本处理");
        r.corrected = true;
      }

      if (version > ConfigStore::kSchemaVersion) {
        schema_too_new_ = true;
        r.Error("插件 " + plugin_id + " 的设置文件 schema_version 为 " +
                std::to_string(version) + "，高于本宿主支持的 " +
                std::to_string(ConfigStore::kSchemaVersion) + "，本次不读也不写该文件");
        return;
      }

      if (version < ConfigStore::kSchemaVersion) {
        toml::table migrated = root;
        if (RunMigrations(&migrated, version, ConfigStore::kSchemaVersion, r)) {
          extra_->source = std::move(migrated);
          r.corrected = true;
        } else {
          r.Warn("插件 " + plugin_id + " 的设置文件从版本 " + std::to_string(version) +
                 " 迁移失败，按默认值并保留能读的项处理");
          r.corrected = true;
        }
      }

      const toml::table* settings = nullptr;
      if (const toml::node* node = extra_->source.get("settings")) {
        settings = node->as_table();
        if (settings == nullptr) {
          r.Warn("[settings] 不是一个表，忽略其内容");
          r.corrected = true;
        }
      } else {
        r.corrected = true;
      }

      for (std::size_t i = 0; i < schema_size_; ++i) {
        const sm::SettingDef& def = schema_[i];
        if (def.name == nullptr) continue;
        const toml::node* node = (settings != nullptr) ? settings->get(def.name) : nullptr;
        if (node == nullptr) {
          r.corrected = true;
          continue;
        }

        Value& value = values_[i];
        switch (def.type) {
          case sm::SettingType::Bool:   ReadBool(*node, def, r, &value.b); break;
          case sm::SettingType::Int:    ReadInt(*node, def, r, &value.i); break;
          case sm::SettingType::Double: ReadDouble(*node, def, r, &value.d); break;
          case sm::SettingType::String: ReadString(*node, def, r, &value.s); break;
          case sm::SettingType::Enum:   ReadEnum(*node, def, r, &value.s); break;
        }
      }

      if (settings != nullptr) {
        for (auto&& entry : *settings) {
          const std::string key(entry.first.str());
          if (index_.find(key) == index_.end()) {
            r.Warn("设置项 " + key + " 不在插件 " + plugin_id +
                   " 声明的 schema 里，本次忽略（保留在文件中）");
          }
        }
      }

      for (auto&& entry : extra_->source) {
        const std::string key(entry.first.str());
        if (key != "schema_version" && key != "settings") {
          r.Warn("插件 " + plugin_id + " 的设置文件里有多余的顶层键 " + key + "，本次忽略");
        }
      }
    }
  }

  dirty_ = dirty_ || r.corrected;
}

bool PluginSettings::SetBool(const char* name, bool value) {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return false;
  Value& slot = values_[it->second];
  if (slot.b == value) return false;
  slot.b = value;
  dirty_ = true;
  if (owner_ != nullptr) owner_->ArmWriteTimer();
  return true;
}

bool PluginSettings::SetInt(const char* name, std::int64_t value) {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return false;
  Value& slot = values_[it->second];
  if (slot.i == value) return false;
  slot.i = value;
  dirty_ = true;
  if (owner_ != nullptr) owner_->ArmWriteTimer();
  return true;
}

bool PluginSettings::SetDouble(const char* name, double value) {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return false;
  Value& slot = values_[it->second];
  if (slot.d == value) return false;
  slot.d = value;
  dirty_ = true;
  if (owner_ != nullptr) owner_->ArmWriteTimer();
  return true;
}

bool PluginSettings::SetString(const char* name, const std::string& value) {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return false;
  Value& slot = values_[it->second];
  if (slot.s == value) return false;
  slot.s = value;
  dirty_ = true;
  if (owner_ != nullptr) owner_->ArmWriteTimer();
  return true;
}

bool PluginSettings::GetBool(const char* name, bool fallback) const {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return fallback;
  return values_[it->second].b;
}

std::int64_t PluginSettings::GetInt(const char* name, std::int64_t fallback) const {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return fallback;
  return values_[it->second].i;
}

double PluginSettings::GetDouble(const char* name, double fallback) const {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return fallback;
  return values_[it->second].d;
}

const char* PluginSettings::GetString(const char* name, const char* fallback) const {
  const auto it = index_.find(name != nullptr ? name : "");
  if (it == index_.end()) return fallback;
  return values_[it->second].s.c_str();
}

void PluginSettings::ResetToDefaults() {
  extra_.reset();
  FillDefaults();
  dirty_ = true;
  if (owner_ != nullptr) owner_->ArmWriteTimer();
}

sm::Span<const sm::SettingDef> PluginSettings::Schema() const {
  return sm::Span<const sm::SettingDef>{schema_, schema_size_};
}

ConfigStore::LoadResult ConfigStore::Load(const Paths& paths, bool read_only,
                                          Diagnostics* out) {
  paths_ = paths;
  read_only_ = read_only;
  config_ = Config{};
  extra_.reset();
  plugin_settings_.clear();
  pending_diagnostics_.clear();
  dirty_ = false;
  first_dirty_ms_ = 0;
  last_dirty_ms_ = 0;
  retry_at_ms_ = 0;
  retry_count_ = 0;
  retry_exhausted_ = false;
  last_write_failed_ = false;
  schema_too_new_ = false;
  config_reset_ = false;

  util::ensure_directory(paths_.config_dir);
  util::ensure_directory(paths_.plugin_config_dir);
  util::ensure_directory(paths_.data_dir);
  util::ensure_directory(paths_.log_dir);

  Reader r;
  r.file = paths_.config_file.c_str();
  r.out = out;

  std::string text;
  if (!util::file_exists(paths_.config_file)) {
    r.Info("配置文件不存在，按默认值生成：" + paths_.config_file);
    r.corrected = true;
  } else if (!util::read_text_file(paths_.config_file, &text)) {
    r.Error("配置文件无法读取，本次按默认值运行：" + paths_.config_file);
    r.corrected = true;
    config_reset_ = true;
  } else {
    toml::table parsed;
    std::string failure;
    if (!ParseToml(text, paths_.config_file, &parsed, &failure)) {
      RenameCorrupt(paths_.config_file);
      r.Error("配置文件解析失败：" + failure + "，原文件已改名为 " +
              util::path_filename(paths_.config_file) + ".corrupt-*，按默认值重新生成");
      r.corrected = true;
      config_reset_ = true;
    } else {
      extra_.reset(new Extra{std::move(parsed)});
    }
  }

  if (extra_ != nullptr) {
    std::int32_t version = kSchemaVersion;
    if (const toml::node* node = extra_->source.get("schema_version")) {
      if (const auto* integer = node->as_integer()) {
        version = static_cast<std::int32_t>(integer->get());
      } else {
        r.Warn("schema_version 不是整数，按当前版本处理");
        r.corrected = true;
      }
    } else {
      r.Warn("缺少 schema_version，按当前版本处理");
      r.corrected = true;
    }

    if (version > kSchemaVersion) {
      r.Error("配置文件的 schema_version 为 " + std::to_string(version) +
              "，高于本宿主支持的 " + std::to_string(kSchemaVersion) +
              "。请升级 SimpleMessage，或删除该文件后重新启动：" + paths_.config_file);
      schema_too_new_ = true;
      return LoadResult::SchemaTooNew;
    }

    if (version < kSchemaVersion) {
      toml::table migrated = extra_->source;
      if (RunMigrations(&migrated, version, kSchemaVersion, r)) {
        extra_->source = std::move(migrated);
        r.corrected = true;
      } else {
        r.Warn("配置从版本 " + std::to_string(version) +
               " 迁移失败，按默认值并保留能读的项处理");
        r.corrected = true;
      }
    }
  }

  const toml::table* app = nullptr;
  if (extra_ != nullptr) {
    if (const toml::node* node = extra_->source.get("app")) {
      app = node->as_table();
      if (app == nullptr) {
        r.Warn("[app] 不是一个表，本节按默认值处理");
        r.corrected = true;
      }
    } else {
      r.corrected = true;
    }
  }

  auto read_string = [&](const toml::table* table, const char* section, const char* key,
                         const char* fallback,
                         std::initializer_list<const char*> allowed) -> std::string {
    if (table == nullptr) return fallback;
    const toml::node* node = table->get(key);
    if (node == nullptr) {
      r.corrected = true;
      return fallback;
    }
    const auto* value = node->as_string();
    if (value == nullptr) {
      r.Warn(std::string("[") + section + "] " + key + " 不是字符串，改用默认值 " +
             fallback);
      r.corrected = true;
      return fallback;
    }
    const std::string text = value->get();
    if (allowed.size() == 0) return text;
    for (const char* option : allowed) {
      if (text == option) return text;
    }
    r.Warn(std::string("[") + section + "] " + key + " 的值 \"" + text +
           "\" 不是允许的取值之一，改用默认值 " + fallback);
    r.corrected = true;
    return fallback;
  };

  auto read_bool = [&](const toml::table* table, const char* section, const char* key,
                       bool fallback) -> bool {
    if (table == nullptr) {
      r.corrected = true;
      return fallback;
    }
    const toml::node* node = table->get(key);
    if (node == nullptr) {
      r.corrected = true;
      return fallback;
    }
    const auto* value = node->as_boolean();
    if (value == nullptr) {
      r.Warn(std::string("[") + section + "] " + key + " 不是布尔值，改用默认值");
      r.corrected = true;
      return fallback;
    }
    return value->get();
  };

  auto read_int = [&](const toml::table* table, const char* section, const char* key,
                      std::int64_t fallback, std::int64_t lo,
                      std::int64_t hi) -> std::int64_t {
    if (table == nullptr) {
      r.corrected = true;
      return fallback;
    }
    const toml::node* node = table->get(key);
    if (node == nullptr) {
      r.corrected = true;
      return fallback;
    }
    const auto* value = node->as_integer();
    if (value == nullptr) {
      r.Warn(std::string("[") + section + "] " + key + " 不是整数，改用默认值 " +
             std::to_string(fallback));
      r.corrected = true;
      return fallback;
    }
    std::int64_t number = value->get();
    if (number < lo || number > hi) {
      const std::int64_t clamped = (number < lo) ? lo : hi;
      r.Warn(std::string("[") + section + "] " + key + " = " + std::to_string(number) +
             " 超出范围 [" + std::to_string(lo) + ", " + std::to_string(hi) + "]，夹到 " +
             std::to_string(clamped));
      number = clamped;
      r.corrected = true;
    }
    return number;
  };

  const std::string notify_level =
      read_string(app, "app", "notify_log_level", "error", {"off", "error", "warn", "info"});
  config_.app.notify_log_level = (notify_level == "off")     ? NotifyLogLevel::Off
                                 : (notify_level == "warn")  ? NotifyLogLevel::Warn
                                 : (notify_level == "info")  ? NotifyLogLevel::Info
                                                             : NotifyLogLevel::Error;

  const std::string log_level =
      read_string(app, "app", "log_level", "info", {"trace", "debug", "info", "warn", "error"});
  config_.app.log_level = (log_level == "trace")     ? sm::LogLevel::Trace
                          : (log_level == "debug")   ? sm::LogLevel::Debug
                          : (log_level == "warn")    ? sm::LogLevel::Warn
                          : (log_level == "error")   ? sm::LogLevel::Error
                                                     : sm::LogLevel::Info;

  config_.app.log_retention_days =
      static_cast<std::int32_t>(read_int(app, "app", "log_retention_days", 7, 1, 365));
  config_.app.log_max_total_mb =
      static_cast<std::int32_t>(read_int(app, "app", "log_max_total_mb", 32, 1, 1024));
  config_.app.ui_font_size =
      static_cast<std::int32_t>(read_int(app, "app", "ui_font_size", 16, 10, 32));
  config_.app.single_instance = read_bool(app, "app", "single_instance", true);
  config_.app.notification_history_size = static_cast<std::int32_t>(read_int(
      app, "app", "notification_history_size",
      static_cast<std::int64_t>(sm::limits::kNotificationHistoryCapacity), 1,
      static_cast<std::int64_t>(sm::limits::kNotificationHistoryCapacity)));

  WarnUnknownKeys(app, "app",
                  {"notify_log_level", "log_level", "log_retention_days",
                   "log_max_total_mb", "ui_font_size", "single_instance",
                   "notification_history_size"},
                  &r);

  const toml::table* notify_window = nullptr;
  if (extra_ != nullptr) {
    if (const toml::node* node = extra_->source.get("notify_window")) {
      notify_window = node->as_table();
      if (notify_window == nullptr) {
        r.Warn("[notify_window] 不是一个表，本节按默认值处理");
        r.corrected = true;
      }
    } else {
      r.corrected = true;
    }
  }

  config_.notify_window.monitor =
      read_string(notify_window, "notify_window", "monitor", "primary", {});
  const std::string corner = read_string(notify_window, "notify_window", "position",
                                         "bottom_right",
                                         {"top_left", "top_right", "bottom_left",
                                          "bottom_right"});
  config_.notify_window.corner =
      (corner == "top_left")      ? NotifyCorner::kTopLeft
      : (corner == "top_right")   ? NotifyCorner::kTopRight
      : (corner == "bottom_left") ? NotifyCorner::kBottomLeft
                                  : NotifyCorner::kBottomRight;

  WarnUnknownKeys(notify_window, "notify_window", {"monitor", "position"}, &r);

  if (extra_ != nullptr) {
    if (const toml::node* node = extra_->source.get("plugins")) {
      const toml::table* plugins = node->as_table();
      if (plugins == nullptr) {
        r.Warn("[plugins] 不是一个表，本次忽略");
        r.corrected = true;
      } else {
        for (auto&& entry : *plugins) {
          const std::string id(entry.first.str());
          if (const auto* value = entry.second.as_boolean()) {
            config_.plugins[id] = value->get();
          } else {
            r.Warn("[plugins] 中 " + id + " 的值不是布尔值，按\"启用\"处理");
            config_.plugins[id] = true;
            r.corrected = true;
          }
        }
      }
    }
  }

  if (extra_ != nullptr) {
    if (const toml::node* node = extra_->source.get("routing")) {
      const toml::table* routing = node->as_table();
      if (routing == nullptr) {
        r.Warn("[routing] 不是一个表，本次忽略");
        r.corrected = true;
      } else {
        for (auto&& entry : *routing) {
          const std::string id(entry.first.str());
          const toml::array* array = entry.second.as_array();
          if (array == nullptr) {
            r.Warn("[routing] 中 " + id + " 的值不是字符串数组，本次忽略该项");
            r.corrected = true;
            continue;
          }
          std::vector<std::string> patterns;
          bool valid = true;
          for (auto&& element : *array) {
            if (const auto* pattern = element.as_string()) {
              patterns.push_back(pattern->get());
            } else {
              r.Warn("[routing] 中 " + id + " 的数组里出现了非字符串项，本次忽略该项");
              r.corrected = true;
              valid = false;
              break;
            }
          }
          if (valid) config_.routing[id] = std::move(patterns);
        }
      }
    }
  }

  if (extra_ != nullptr) {
    for (auto&& entry : extra_->source) {
      const std::string key(entry.first.str());
      if (key != "schema_version" && key != "app" && key != "notify_window" &&
          key != "plugins" && key != "routing") {
        r.Warn("配置里有未知的顶层键 " + key + "，本次忽略（保留在文件中）");
      }
    }
  }

  if (r.corrected) MarkDirty();
  return LoadResult::Ok;
}

void ConfigStore::SetPluginEnabled(const std::string& plugin_id, bool enabled) {
  const auto it = config_.plugins.find(plugin_id);
  if (it != config_.plugins.end() && it->second == enabled) return;
  config_.plugins[plugin_id] = enabled;
  MarkDirty();
}

void ConfigStore::SetRouting(const std::string& plugin_id,
                             std::vector<std::string> patterns) {
  config_.routing[plugin_id] = std::move(patterns);
  MarkDirty();
}

void ConfigStore::SetLogLevel(sm::LogLevel level) {
  if (config_.app.log_level == level) return;
  config_.app.log_level = level;
  MarkDirty();
}

void ConfigStore::SetNotifyLogLevel(NotifyLogLevel level) {
  if (config_.app.notify_log_level == level) return;
  config_.app.notify_log_level = level;
  MarkDirty();
}

void ConfigStore::SetUiFontSize(std::int32_t size) {
  if (config_.app.ui_font_size == size) return;
  config_.app.ui_font_size = size;
  MarkDirty();
}

void ConfigStore::SetNotifyWindow(const std::string& monitor, NotifyCorner corner) {
  if (config_.notify_window.monitor == monitor && config_.notify_window.corner == corner) {
    return;
  }
  config_.notify_window.monitor = monitor;
  config_.notify_window.corner = corner;
  MarkDirty();
}

void ConfigStore::SetNotificationHistorySize(std::int32_t size) {
  const std::int32_t clamped = (size < 1) ? 1 : size;
  if (config_.app.notification_history_size == clamped) return;
  config_.app.notification_history_size = clamped;
  MarkDirty();
}

PluginSettings& ConfigStore::EnsurePluginSettings(const std::string& plugin_id,
                                                  sm::Span<const sm::SettingDef> schema,
                                                  Diagnostics* out) {
  PluginSettings& settings = plugin_settings_[plugin_id];
  if (settings.loaded()) return settings;

  util::ensure_directory(paths_.plugin_config_dir);
  const std::string path = util::path_join(paths_.plugin_config_dir, plugin_id + ".toml");
  settings.owner_ = this;
  settings.Load(plugin_id, path, schema, out);
  if (settings.schema_too_new()) schema_too_new_ = true;
  if (settings.dirty()) ArmWriteTimer();
  return settings;
}

bool ConfigStore::HasPluginSettings(const std::string& plugin_id) const {
  return plugin_settings_.find(plugin_id) != plugin_settings_.end();
}

PluginSettings& ConfigStore::PluginSettingsFor(const std::string& plugin_id) {
  return plugin_settings_[plugin_id];
}

void ConfigStore::ResetPluginSettings(const std::string& plugin_id, Diagnostics* out) {
  const auto it = plugin_settings_.find(plugin_id);
  if (it == plugin_settings_.end()) return;

  PluginSettings& settings = it->second;
  util::remove_file(settings.file_path_);
  util::remove_file(settings.file_path_ + ".bak");
  settings.ResetToDefaults();
  AddInfo(out, "插件 " + plugin_id + " 的设置已重置为默认值");
}

void ConfigStore::ArmWriteTimer() {
  if (read_only_ || schema_too_new_) return;
  const std::int64_t now = platform::monotonic_ms();
  if (first_dirty_ms_ == 0) first_dirty_ms_ = now;
  last_dirty_ms_ = now;
  retry_at_ms_ = 0;
  retry_count_ = 0;
  retry_exhausted_ = false;
}

void ConfigStore::MarkDirty() {
  if (read_only_ || schema_too_new_) return;
  dirty_ = true;
  ArmWriteTimer();
}

void ConfigStore::Tick(std::int64_t now_ms) { Advance(now_ms, false); }

void ConfigStore::FlushNow() { Advance(platform::monotonic_ms(), true); }

bool ConfigStore::has_pending_write() const {
  if (dirty_) return true;
  for (const auto& entry : plugin_settings_) {
    if (entry.second.dirty()) return true;
  }
  return false;
}

std::int64_t ConfigStore::next_write_due_ms() const {
  if (read_only_ || schema_too_new_ || retry_exhausted_ || !has_pending_write()) return 0;
  if (retry_at_ms_ != 0) return retry_at_ms_;
  const std::int64_t by_idle = last_dirty_ms_ + kDebounceMs;
  const std::int64_t by_cap = first_dirty_ms_ + kMaxDebounceMs;
  return (by_idle < by_cap) ? by_idle : by_cap;
}

void ConfigStore::Advance(std::int64_t now_ms, bool force) {
  if (read_only_ || schema_too_new_ || !has_pending_write()) return;
  if (!force) {
    if (retry_exhausted_) return;
    if (retry_at_ms_ != 0) {
      if (now_ms < retry_at_ms_) return;
    } else if (now_ms < last_dirty_ms_ + kDebounceMs &&
               now_ms < first_dirty_ms_ + kMaxDebounceMs) {
      return;
    }
  }
  WritePending(now_ms);
}

void ConfigStore::WritePending(std::int64_t now_ms) {
  bool ok = true;
  if (dirty_) {
    if (WriteConfigFile()) {
      dirty_ = false;
    } else {
      ok = false;
    }
  }
  for (auto& entry : plugin_settings_) {
    if (!entry.second.dirty()) continue;
    if (WritePluginFile(entry.second)) {
      entry.second.dirty_ = false;
    } else {
      ok = false;
    }
  }

  if (ok) {
    last_write_failed_ = false;
    retry_count_ = 0;
    retry_exhausted_ = false;
    retry_at_ms_ = 0;
    first_dirty_ms_ = 0;
    last_dirty_ms_ = 0;
    return;
  }

  last_write_failed_ = true;
  ++retry_count_;
  static constexpr std::int64_t kRetryDelays[kMaxRetries] = {1000, 2000, 4000};
  if (retry_count_ > kMaxRetries) {
    retry_exhausted_ = true;
    retry_at_ms_ = 0;
    NoteError("配置写回连续失败 " + std::to_string(kMaxRetries) +
              " 次，已暂停自动重试；退出前会再尝试一次");
    return;
  }
  retry_at_ms_ = now_ms + kRetryDelays[retry_count_ - 1];
  Note("配置写回失败，将在 " + std::to_string(kRetryDelays[retry_count_ - 1] / 1000) +
       " 秒后重试");
}

bool ConfigStore::WriteConfigFile() {
  toml::table root;
  if (extra_ != nullptr) root = extra_->source;
  root.insert_or_assign("schema_version", static_cast<std::int64_t>(kSchemaVersion));

  toml::table app;
  if (const toml::node* node = root.get("app")) {
    if (const toml::table* existing = node->as_table()) app = *existing;
  }
  app.insert_or_assign("notify_log_level", std::string(NotifyLevelName(config_.app.notify_log_level)));
  app.insert_or_assign("log_level", std::string(LogLevelName(config_.app.log_level)));
  app.insert_or_assign("log_retention_days", static_cast<std::int64_t>(config_.app.log_retention_days));
  app.insert_or_assign("log_max_total_mb", static_cast<std::int64_t>(config_.app.log_max_total_mb));
  app.insert_or_assign("ui_font_size", static_cast<std::int64_t>(config_.app.ui_font_size));
  app.insert_or_assign("single_instance", config_.app.single_instance);
  app.insert_or_assign("notification_history_size",
                       static_cast<std::int64_t>(config_.app.notification_history_size));
  root.insert_or_assign("app", std::move(app));

  toml::table notify_window;
  if (const toml::node* node = root.get("notify_window")) {
    if (const toml::table* existing = node->as_table()) notify_window = *existing;
  }
  notify_window.insert_or_assign("monitor", config_.notify_window.monitor);
  notify_window.insert_or_assign("position", CornerName(config_.notify_window.corner));
  root.insert_or_assign("notify_window", std::move(notify_window));

  toml::table plugins;
  if (const toml::node* node = root.get("plugins")) {
    if (const toml::table* existing = node->as_table()) plugins = *existing;
  }
  for (const auto& entry : config_.plugins) {
    plugins.insert_or_assign(entry.first, entry.second);
  }
  root.insert_or_assign("plugins", std::move(plugins));

  toml::table routing;
  if (const toml::node* node = root.get("routing")) {
    if (const toml::table* existing = node->as_table()) routing = *existing;
  }
  for (const auto& entry : config_.routing) {
    toml::array patterns;
    for (const std::string& pattern : entry.second) patterns.push_back(pattern);
    routing.insert_or_assign(entry.first, std::move(patterns));
  }
  root.insert_or_assign("routing", std::move(routing));

  std::ostringstream stream;
  stream << root;

  std::string error;
  if (!util::write_text_file_atomically(paths_.config_file, stream.str(), &error)) {
    NoteError(error);
    return false;
  }
  return true;
}

bool ConfigStore::WritePluginFile(PluginSettings& settings) {
  toml::table root;
  if (settings.extra_ != nullptr) root = settings.extra_->source;
  root.insert_or_assign("schema_version", static_cast<std::int64_t>(kSchemaVersion));

  toml::table values;
  if (const toml::node* node = root.get("settings")) {
    if (const toml::table* existing = node->as_table()) values = *existing;
  }
  for (std::size_t i = 0; i < settings.schema_size_; ++i) {
    const sm::SettingDef& def = settings.schema_[i];
    if (def.name == nullptr) continue;
    const PluginSettings::Value& value = settings.values_[i];
    const std::string key(def.name);
    switch (def.type) {
      case sm::SettingType::Bool:
        values.insert_or_assign(key, value.b);
        break;
      case sm::SettingType::Int:
        values.insert_or_assign(key, value.i);
        break;
      case sm::SettingType::Double:
        values.insert_or_assign(key, value.d);
        break;
      case sm::SettingType::String:
      case sm::SettingType::Enum:
        values.insert_or_assign(key, value.s);
        break;
    }
  }
  root.insert_or_assign("settings", std::move(values));

  std::ostringstream stream;
  stream << root;

  std::string error;
  if (!util::write_text_file_atomically(settings.file_path_, stream.str(), &error)) {
    NoteError(error);
    return false;
  }
  return true;
}

void ConfigStore::DrainDiagnostics(Diagnostics* out) {
  if (out == nullptr) {
    pending_diagnostics_.clear();
    return;
  }
  for (Diagnostic& item : pending_diagnostics_) out->push_back(std::move(item));
  pending_diagnostics_.clear();
}

void ConfigStore::Note(std::string message) {
  if (pending_diagnostics_.size() >= kMaxPendingDiagnostics) {
    pending_diagnostics_.erase(pending_diagnostics_.begin());
  }
  pending_diagnostics_.push_back(
      Diagnostic{Diagnostic::Severity::Warn, "[配置写回] " + std::move(message)});
}

void ConfigStore::NoteError(std::string message) {
  if (pending_diagnostics_.size() >= kMaxPendingDiagnostics) {
    pending_diagnostics_.erase(pending_diagnostics_.begin());
  }
  pending_diagnostics_.push_back(
      Diagnostic{Diagnostic::Severity::Error, "[配置写回] " + std::move(message)});
}

}  // namespace config
}  // namespace sm

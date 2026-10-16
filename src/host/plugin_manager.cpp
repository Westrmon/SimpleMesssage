// SimpleMessage — 插件管理器的实现
#include "host/plugin_manager.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

#include "sm/version.h"
#include "sm/version_range.h"

#include "host/log.h"
#include "host/util.h"
#include "platform/platform.h"

namespace smlog = sm::log;

namespace sm {
namespace host {

using AbiVersionFn    = std::int32_t (*)();
using PluginMetaFn    = const sm::PluginMeta* (*)();
using CreatePluginFn  = sm::IPlugin* (*)(sm::IHostContext*);
using DestroyPluginFn = void (*)(sm::IPlugin*);

namespace {

template <typename Fn>
Fn FetchSymbol(sm::platform::IModule* module, const char* name) {
  void* const address = module->symbol(name);
  Fn fn = nullptr;
  if (address != nullptr) {
    static_assert(sizeof(fn) == sizeof(address), "函数指针与数据指针尺寸不同");
    std::memcpy(&fn, &address, sizeof(fn));
  }
  return fn;
}

bool EqualsNoCase(const std::string& a, const char* b) {
  if (b == nullptr) return false;
  std::size_t i = 0;
  for (; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (cb == '\0') return false;
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return b[i] == '\0';
}

bool EndsWithNoCase(const std::string& text, const char* suffix) {
  const std::size_t n = std::strlen(suffix);
  if (text.size() < n) return false;
  return EqualsNoCase(text.substr(text.size() - n), suffix);
}

bool IsValidIdChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
         c == '-';
}

bool IsValidSettingNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::string Fmt(const char* fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  return std::string(buffer);
}

struct InitializeCall {
  sm::IPlugin* instance;
  bool         ok;
};
void TrampolineInitialize(void* p) {
  auto* const call = static_cast<InitializeCall*>(p);
  call->ok = call->instance->initialize();
}

struct StartCallData {
  sm::IPlugin*    instance;
  sm::StartResult result;
  sm::StartOutput out;
};
void TrampolineStart(void* p) {
  auto* const call = static_cast<StartCallData*>(p);
  call->result = call->instance->start(&call->out);
}

struct InstanceCall {
  sm::IPlugin* instance;
};
void TrampolineStop(void* p) { static_cast<InstanceCall*>(p)->instance->stop(); }
void TrampolineExit(void* p) { static_cast<InstanceCall*>(p)->instance->exit(); }
void TrampolineDrawView(void* p) { static_cast<InstanceCall*>(p)->instance->draw_view(); }

struct NotifyCall {
  sm::IPlugin*            instance;
  const sm::Notification* notification;
};
void TrampolineNotify(void* p) {
  auto* const call = static_cast<NotifyCall*>(p);
  call->instance->on_notify(*call->notification);
}

struct ActionCall {
  sm::IPlugin*            instance;
  const sm::Notification* notification;
  const char*             action_id;
};
void TrampolineAction(void* p) {
  auto* const call = static_cast<ActionCall*>(p);
  call->instance->on_action(*call->notification, call->action_id);
}

struct DrawUiCall {
  sm::IPlugin*                     instance;
  sm::Span<const sm::Notification> history;
};
void TrampolineDrawUi(void* p) {
  auto* const call = static_cast<DrawUiCall*>(p);
  call->instance->draw_ui(call->history);
}

struct TaskCall {
  sm::MainThreadTaskFn fn;
  void*                user;
};
void TrampolineTask(void* p) {
  auto* const call = static_cast<TaskCall*>(p);
  call->fn(call->user);
}

struct AbiCall {
  AbiVersionFn fn;
  std::int32_t value;
};
void TrampolineAbiVersion(void* p) {
  auto* const call = static_cast<AbiCall*>(p);
  call->value = call->fn();
}

struct MetaCall {
  PluginMetaFn           fn;
  const sm::PluginMeta*  value;
};
void TrampolinePluginMeta(void* p) {
  auto* const call = static_cast<MetaCall*>(p);
  call->value = call->fn();
}

struct CreateCall {
  CreatePluginFn   fn;
  sm::IHostContext* host;
  sm::IPlugin*      value;
};
void TrampolineCreate(void* p) {
  auto* const call = static_cast<CreateCall*>(p);
  call->value = call->fn(call->host);
}

struct DestroyCall {
  DestroyPluginFn fn;
  sm::IPlugin*    instance;
};
void TrampolineDestroy(void* p) {
  auto* const call = static_cast<DestroyCall*>(p);
  call->fn(call->instance);
}

void LogCrashWhileUnloading(const std::string& id, const char* method, bool crashed_before,
                            const guard::Outcome& outcome) {
  const char* const text = guard::ExceptionText(outcome.exception_code);
  smlog::WriteCriticalf(sm::LogLevel::Error, id.c_str(),
                        crashed_before
                            ? "插件在 %s() 内再次崩溃（异常码 0x%08X%s%s）；继续执行卸载"
                            : "插件在 %s() 内崩溃（异常码 0x%08X%s%s）；崩溃被捕获，卸载继续",
                        method, static_cast<unsigned>(outcome.exception_code),
                        text != nullptr ? "，" : "", text != nullptr ? text : "");
}

void LogConfigDiagnostics(const std::string& plugin_id, const config::Diagnostics& diagnostics) {
  for (const config::Diagnostic& diagnostic : diagnostics) {
    sm::LogLevel level = sm::LogLevel::Info;
    if (diagnostic.severity == config::Diagnostic::Severity::Warn) level = sm::LogLevel::Warn;
    if (diagnostic.severity == config::Diagnostic::Severity::Error) level = sm::LogLevel::Error;
    smlog::Write(level, plugin_id.c_str(), diagnostic.message.c_str());
  }
}

}  // namespace

const char* PluginStateText(PluginState state) {
  switch (state) {
    case PluginState::Discovered:       return "已发现";
    case PluginState::Rejected:         return "拒绝加载";
    case PluginState::Loaded:           return "已加载";
    case PluginState::Initialized:      return "已初始化";
    case PluginState::InitializeFailed: return "初始化失败";
    case PluginState::Running:          return "运行中";
    case PluginState::Stopped:          return "已停用";
    case PluginState::Disabled:         return "已禁用";
    case PluginState::Unloaded:         return "已卸载";
  }
  return "未知状态";
}

struct PluginManager::Record {
  std::string id;
  std::string path;
  PluginState state = PluginState::Discovered;
  std::string reason;

  bool enabled = true;
  bool auto_disabled = false;
  bool pending_teardown = false;
  bool skip_stop = false;
  bool stop_pending = false;
  bool exit_pending = false;

  std::atomic<bool> started_once{false};

  std::atomic<std::int64_t> last_start_unix_ms{0};
  std::atomic<std::int32_t> last_start_result{
      static_cast<std::int32_t>(sm::StartResult::Empty)};
  std::atomic<bool> last_start_crashed{false};
  std::int64_t last_notify_unix_ms = 0;
  bool         last_notify_crashed = false;

  sm::platform::IModule* module = nullptr;
  const sm::PluginMeta*  meta = nullptr;
  sm::IPlugin*           instance = nullptr;
  CreatePluginFn         create_fn = nullptr;
  DestroyPluginFn        destroy_fn = nullptr;
  std::unique_ptr<PluginHost> host;
  config::PluginSettings*     settings = nullptr;
  std::vector<const char*> subscriptions;

  std::string    meta_name;
  std::string    meta_version;
  std::string    meta_author;
  std::string    meta_description;
  std::string    meta_host_range;
  sm::PluginKind meta_kind = sm::PluginKind::Source;
  std::size_t    meta_setting_count = 0;
};

PluginManager::PluginManager(config::ConfigStore& config) : config_(config) {}

PluginManager::~PluginManager() {
  for (auto& holder : records_) {
    if (holder->module == nullptr) continue;
    smlog::Writef(sm::LogLevel::Warn, holder->id.c_str(),
                  "插件未被显式关停，在插件管理器析构时释放其模块");
    platform::unload_module(holder->module);
    holder->module = nullptr;
  }
}

void PluginManager::SetScheduleSink(IScheduleSink* sink) {
  scheduler_ = sink;
}

void PluginManager::SetUiSession(IUiSession* session) { ui_ = session; }
void PluginManager::SetActionSink(IPluginActionSink* sink) { actions_ = sink; }

void PluginManager::SetEventSink(IPluginEventSink* sink) { events_ = sink; }

PluginHostSetup PluginManager::MakeHostSetup(const Record& record) const {
  PluginHostSetup setup;
  setup.plugin_id = record.id;
  setup.own_config_path =
      util::path_join(config_.PathsRef().plugin_config_dir, record.id + ".toml");
  setup.settings = record.settings;
  setup.env.paths = &config_.PathsRef();
  setup.env.config = &config_;
  setup.env.scheduler = scheduler_;
  setup.env.ui = ui_;
  setup.env.actions = actions_;
  setup.env.shutting_down = &shutting_down_;
  setup.post_queue = &post_queue_;
  return setup;
}

PluginManager::Record* PluginManager::AddRecord(const std::string& id,
                                                const std::string& path) {
  auto record = std::make_unique<Record>();
  record->id = id;
  record->path = path;
  const auto it = config_.Data().plugins.find(id);
  record->enabled = (it != config_.Data().plugins.end()) ? it->second : true;

  Record* const raw = record.get();
  records_.push_back(std::move(record));
  index_[id] = raw;
  return raw;
}

PluginManager::Record* PluginManager::FindRecord(const std::string& id) const {
  const auto it = index_.find(id);
  return it != index_.end() ? it->second : nullptr;
}

void PluginManager::Discover(const std::string& plugin_dir) {
  if (discovered_) {
    smlog::Write(sm::LogLevel::Warn, "plugin", "插件目录重复扫描，已忽略本次请求");
    return;
  }
  discovered_ = true;
  plugin_dir_ = plugin_dir;

  for (const util::DirEntry& entry : util::list_files(plugin_dir, nullptr)) {
    if (!EndsWithNoCase(entry.name, ".dll")) continue;
    if (EqualsNoCase(entry.name, "sm_imgui.dll")) continue;

    const std::string stem = entry.name.substr(0, entry.name.size() - 4);
    Record* const record = AddRecord(stem, util::path_join(plugin_dir, entry.name));
    TryLoadAndValidate(*record);
  }

  const Stats stats = SnapshotStats();
  smlog::Writef(sm::LogLevel::Info, "plugin",
                "插件目录 %s：扫描到 %d 个，通过校验 %d 个，拒绝 %d 个",
                plugin_dir.c_str(), stats.scanned, stats.accepted, stats.rejected);
}

void PluginManager::TryLoadAndValidate(Record& record) {
  record.state = PluginState::Discovered;
  record.reason.clear();

  const char* load_error = nullptr;
  record.module = platform::load_module(record.path.c_str(), &load_error);
  if (record.module == nullptr) {
    RejectRecord(record, Fmt("加载失败：%s", load_error != nullptr ? load_error : "未知原因"));
    return;
  }

  const AbiVersionFn    abi_fn = FetchSymbol<AbiVersionFn>(record.module, "sm_abi_version");
  const PluginMetaFn    meta_fn = FetchSymbol<PluginMetaFn>(record.module, "sm_plugin_meta");
  const CreatePluginFn  create_fn = FetchSymbol<CreatePluginFn>(record.module, "sm_create_plugin");
  const DestroyPluginFn destroy_fn =
      FetchSymbol<DestroyPluginFn>(record.module, "sm_destroy_plugin");

  std::string missing;
  if (abi_fn == nullptr) missing += " sm_abi_version";
  if (meta_fn == nullptr) missing += " sm_plugin_meta";
  if (create_fn == nullptr) missing += " sm_create_plugin";
  if (destroy_fn == nullptr) missing += " sm_destroy_plugin";
  if (!missing.empty()) {
    RejectRecord(record, "缺少导出符号：" + missing);
    return;
  }
  record.create_fn = create_fn;
  record.destroy_fn = destroy_fn;

  AbiCall abi_call{abi_fn, 0};
  const guard::Outcome abi_outcome = guard::Invoke(&TrampolineAbiVersion, &abi_call);
  if (abi_outcome.crashed()) {
    guard::ReportCrash(record.id.c_str(), "sm_abi_version", abi_outcome);
    RejectRecord(record, "读取 ABI 版本时崩溃");
    return;
  }
  if (abi_call.value != SM_API_VERSION) {
    RejectRecord(record, Fmt("ABI 版本不匹配：插件是 %d，宿主是 %d", abi_call.value,
                             static_cast<int>(SM_API_VERSION)));
    return;
  }

  MetaCall meta_call{meta_fn, nullptr};
  const guard::Outcome meta_outcome = guard::Invoke(&TrampolinePluginMeta, &meta_call);
  if (meta_outcome.crashed()) {
    guard::ReportCrash(record.id.c_str(), "sm_plugin_meta", meta_outcome);
    RejectRecord(record, "读取元信息时崩溃");
    return;
  }
  if (meta_call.value == nullptr) {
    RejectRecord(record, "sm_plugin_meta() 返回空指针");
    return;
  }

  const ValidationResult validation = Validate(record, *meta_call.value);
  if (!validation.ok) {
    RejectRecord(record, validation.reason);
    return;
  }

  record.meta = meta_call.value;
  record.subscriptions = std::move(validation.subscriptions);

  const std::string file_stem = record.id;
  const std::string canonical_id = record.meta->id;
  if (canonical_id != file_stem) {
    index_.erase(file_stem);
    record.id = canonical_id;
    index_[record.id] = &record;
  }

  record.meta_name = (record.meta->name != nullptr) ? record.meta->name : record.id;
  record.meta_version = (record.meta->version != nullptr) ? record.meta->version : "";
  record.meta_author = (record.meta->author != nullptr) ? record.meta->author : "";
  record.meta_description =
      (record.meta->description != nullptr) ? record.meta->description : "";
  record.meta_host_range =
      (record.meta->host_range != nullptr) ? record.meta->host_range : "";
  record.meta_kind = record.meta->kind;
  record.meta_setting_count = record.meta->settings.size;

  if (config_.Data().plugins.find(record.id) == config_.Data().plugins.end()) {
    config_.SetPluginEnabled(record.id, true);
    record.enabled = true;
  }
}

PluginManager::ValidationResult PluginManager::Validate(const Record& record,
                                                        const sm::PluginMeta& meta) {
  ValidationResult result;

  if (meta.api_version != SM_API_VERSION) {
    return {false, Fmt("meta.api_version 是 %d，本宿主是 %d", meta.api_version,
                       static_cast<int>(SM_API_VERSION)), {}};
  }

  if (meta.id == nullptr || *meta.id == '\0') return {false, "meta.id 为空", {}};
  const std::size_t id_length = std::strlen(meta.id);
  if (id_length > sm::limits::kMaxPluginIdBytes) {
    return {false, Fmt("meta.id 过长（%zu 字节，上限 %zu）", id_length,
                       sm::limits::kMaxPluginIdBytes), {}};
  }
  for (std::size_t i = 0; i < id_length; ++i) {
    if (!IsValidIdChar(meta.id[i])) {
      return {false, Fmt("meta.id \"%s\" 含非法字符 '%c'（只允许 a-z 0-9 _ . -）",
                         meta.id, meta.id[i]), {}};
    }
  }
  if (std::strncmp(meta.id, "sm.", 3) == 0) {
    return {false, Fmt("meta.id \"%s\" 使用了保留前缀 \"sm.\"", meta.id), {}};
  }

  if (!EqualsNoCase(record.id, meta.id)) {
    return {false, Fmt("DLL 文件名 \"%s.dll\" 与 meta.id \"%s\" 不一致", record.id.c_str(),
                       meta.id), {}};
  }

  if (meta.version == nullptr || *meta.version == '\0') {
    return {false, "meta.version 为空", {}};
  }
  sm::Version plugin_version{};
  if (!sm::parse_version(meta.version, &plugin_version)) {
    return {false, Fmt("meta.version \"%s\" 不是合法的版本号（如 1.2.3）", meta.version), {}};
  }

  if (meta.host_range == nullptr) {
    return {false, "meta.host_range 为空指针", {}};
  }
  if (!sm::is_valid_range(meta.host_range)) {
    return {false, Fmt("meta.host_range \"%s\" 语法非法（支持 >= > <= < = 与逗号、竖线）",
                       meta.host_range), {}};
  }
  const char* version_error = nullptr;
  if (!sm::version_satisfies(SM_VERSION_STRING, meta.host_range, &version_error)) {
    return {false,
            Fmt("宿主版本 %s 不满足插件要求的范围 \"%s\"（%s）", SM_VERSION_STRING,
                meta.host_range, version_error != nullptr ? version_error : "原因未知"),
            {}};
  }

  for (const auto& entry : index_) {
    if (entry.second == &record) continue;
    if (entry.second->meta != nullptr && std::strcmp(entry.second->meta->id, meta.id) == 0) {
      return {false,
              Fmt("插件 id \"%s\" 与已加载的 %s 重复", meta.id, entry.second->path.c_str()),
              {}};
    }
  }

  const sm::Span<const sm::SettingDef> settings = meta.settings;
  if (settings.size > 0 && settings.data == nullptr) {
    return {false, "meta.settings 声明了数量却没有数据指针", {}};
  }
  if (settings.size > sm::limits::kMaxSettingItems) {
    return {false, Fmt("设置项过多（%zu 项，上限 %zu）", settings.size,
                       sm::limits::kMaxSettingItems), {}};
  }
  for (std::size_t i = 0; i < settings.size; ++i) {
    const sm::SettingDef& def = settings.data[i];
    const std::size_t ordinal = i + 1;

    if (def.name == nullptr || *def.name == '\0') {
      return {false, Fmt("第 %zu 个设置项没有 name", ordinal), {}};
    }
    const std::size_t name_length = std::strlen(def.name);
    if (name_length > sm::limits::kMaxSettingNameBytes) {
      return {false, Fmt("第 %zu 个设置项的 name \"%s\" 过长（%zu 字节）", ordinal, def.name,
                         name_length), {}};
    }
    for (std::size_t c = 0; c < name_length; ++c) {
      if (!IsValidSettingNameChar(def.name[c])) {
        return {false, Fmt("第 %zu 个设置项的 name \"%s\" 含非法字符 '%c'（只允许 a-z 0-9 _）",
                           ordinal, def.name, def.name[c]), {}};
      }
    }
    if (std::strcmp(def.name, "schema_version") == 0) {
      return {false, Fmt("第 %zu 个设置项用了保留名 schema_version", ordinal), {}};
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (std::strcmp(settings.data[j].name, def.name) == 0) {
        return {false, Fmt("第 %zu 个设置项与第 %zu 个重名（\"%s\"）", ordinal, j + 1, def.name),
                {}};
      }
    }

    const std::int32_t type_value = static_cast<std::int32_t>(def.type);
    if (type_value < static_cast<std::int32_t>(sm::SettingType::Bool) ||
        type_value > static_cast<std::int32_t>(sm::SettingType::Enum)) {
      return {false, Fmt("第 %zu 个设置项的 type 非法（%d）", ordinal, type_value), {}};
    }
    if (def.type == sm::SettingType::Enum) {
      if (def.options.data == nullptr || def.options.size == 0) {
        return {false, Fmt("第 %zu 个设置项是枚举，但没有声明任何选项", ordinal), {}};
      }
      for (std::size_t o = 0; o < def.options.size; ++o) {
        const sm::SettingOption& option = def.options.data[o];
        if (option.value == nullptr || *option.value == '\0') {
          return {false, Fmt("第 %zu 个设置项的第 %zu 个枚举选项缺少 value", ordinal, o + 1), {}};
        }
      }
    }
  }

  const sm::Span<const char*> subscriptions = meta.subscriptions;
  if (subscriptions.size > 0 && subscriptions.data == nullptr) {
    smlog::Writef(sm::LogLevel::Warn, record.id.c_str(),
                  "订阅模式声明了数量却没有数据指针，按未声明订阅处理");
  } else {
    if (subscriptions.size > sm::limits::kMaxSubscriptions) {
      smlog::Writef(sm::LogLevel::Warn, record.id.c_str(),
                    "订阅模式过多（%zu 个，上限 %zu），超出的部分被忽略", subscriptions.size,
                    sm::limits::kMaxSubscriptions);
    }
    for (std::size_t i = 0; i < subscriptions.size; ++i) {
      if (result.subscriptions.size() >= sm::limits::kMaxSubscriptions) break;
      const char* const pattern = subscriptions.data[i];
      if (pattern == nullptr || *pattern == '\0') {
        smlog::Writef(sm::LogLevel::Warn, record.id.c_str(),
                      "第 %zu 个订阅模式为空，已跳过", i + 1);
        continue;
      }
      result.subscriptions.push_back(pattern);
    }
  }

  result.ok = true;
  return result;
}

void PluginManager::RejectRecord(Record& record, const std::string& reason) {
  smlog::Writef(sm::LogLevel::Warn, record.id.c_str(), "插件被拒绝：%s（文件 %s）",
                reason.c_str(), record.path.c_str());

  record.create_fn = nullptr;
  record.destroy_fn = nullptr;
  if (record.module != nullptr) {
    platform::unload_module(record.module);
    record.module = nullptr;
  }
  SetState(record, PluginState::Rejected, reason.c_str());
}

void PluginManager::LoadEnabled() {
  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.state != PluginState::Discovered) continue;
    if (!record.enabled) {
      smlog::Write(sm::LogLevel::Info, record.id.c_str(), "插件未被启用，跳过加载");
      Unload(record);
      continue;
    }
    CreateAndInitialize(record);
  }

  const Stats stats = SnapshotStats();
  smlog::Writef(sm::LogLevel::Info, "plugin",
                "插件加载完成：启用 %d 个、创建实例 %d 个、运行中 %d 个、初始化失败 %d 个、"
                "被拒绝 %d 个",
                stats.enabled, stats.loaded, stats.running, stats.initialize_failed,
                stats.rejected);
}

void PluginManager::CreateAndInitialize(Record& record) {
  config::Diagnostics diagnostics;
  record.settings = &config_.EnsurePluginSettings(record.id, record.meta->settings, &diagnostics);
  LogConfigDiagnostics(record.id, diagnostics);

  record.host = std::make_unique<PluginHost>(MakeHostSetup(record));

  if (record.create_fn == nullptr) {
    SetState(record, PluginState::InitializeFailed, "插件没有导出 sm_create_plugin");
    record.host.reset();
    return;
  }

  CallScope scope(&main_arena_);
  CreateCall call{record.create_fn, record.host.get(), nullptr};
  const guard::Outcome outcome = guard::Invoke(&TrampolineCreate, &call);
  if (outcome.crashed()) {
    EndCallFailed(record.id, "sm_create_plugin", outcome);
    return;
  }
  if (call.value == nullptr) {
    record.host.reset();
    SetState(record, PluginState::InitializeFailed, "sm_create_plugin() 返回空指针");
    return;
  }

  record.instance = call.value;
  record.exit_pending = true;
  SetState(record, PluginState::Loaded, nullptr);

  InitializeInstance(record);
}

bool PluginManager::InitializeInstance(Record& record) {
  if (record.instance == nullptr || record.host == nullptr) return false;

  record.host->ClearError();
  CallScope scope(&main_arena_);
  InitializeCall call{record.instance, false};
  const guard::Outcome outcome = guard::Invoke(&TrampolineInitialize, &call);
  if (outcome.crashed()) {
    EndCallFailed(record.id, "initialize", outcome);
    return false;
  }
  if (!call.ok) {
    const std::string reason = record.host->LastErrorText();
    if (reason.empty()) {
      smlog::Write(sm::LogLevel::Warn, record.id.c_str(),
                   "initialize() 失败：插件没有说明原因");
    }
    SetState(record, PluginState::InitializeFailed,
             reason.empty() ? "initialize() 返回 false" : reason.c_str());
    return false;
  }

  record.stop_pending = true;
  SetState(record, PluginState::Initialized, nullptr);

  if (record.meta != nullptr && record.meta->kind == sm::PluginKind::Source &&
      scheduler_ != nullptr) {
    scheduler_->Add(record.id);
  }
  return true;
}

bool PluginManager::Enable(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr) return false;

  switch (record->state) {
    case PluginState::Discovered:
    case PluginState::Stopped:
    case PluginState::InitializeFailed:
    case PluginState::Disabled:
      break;
    default:
      return false;
  }

  config_.SetPluginEnabled(record->id, true);
  record->enabled = true;
  record->auto_disabled = false;

  if (record->module == nullptr) {
    TryLoadAndValidate(*record);
    if (record->state == PluginState::Rejected) return false;
  }

  if (record->instance != nullptr) {
    InitializeInstance(*record);
  } else {
    CreateAndInitialize(*record);
  }
  return true;
}

bool PluginManager::Disable(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr) return false;

  switch (record->state) {
    case PluginState::Discovered:
    case PluginState::Loaded:
    case PluginState::Initialized:
    case PluginState::InitializeFailed:
    case PluginState::Running:
    case PluginState::Stopped:
      break;
    default:
      return false;
  }

  record->enabled = false;
  config_.SetPluginEnabled(record->id, false);
  record->auto_disabled = false;
  record->skip_stop = false;

  if (record->instance != nullptr) {
    CallScope scope(&main_arena_);
    CallStop(*record);
  }
  if (scheduler_ != nullptr) scheduler_->Remove(record->id);
  SetState(*record, PluginState::Stopped, nullptr);
  return true;
}

bool PluginManager::RetryInitialize(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->state != PluginState::InitializeFailed) return false;
  record->auto_disabled = false;
  return InitializeInstance(*record);
}

void PluginManager::ReleaseDiscoveredModules() {
  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.instance != nullptr || record.module == nullptr) continue;
    Unload(record);
  }
}

void PluginManager::ShutdownAll() {
  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.instance == nullptr) continue;
    if (scheduler_ != nullptr && scheduler_->IsExecuting(record.id)) {
      smlog::WriteCriticalf(sm::LogLevel::Warn, record.id.c_str(),
                            "插件未响应退出请求（仍在执行中），跳过 stop()");
      continue;
    }
    CallScope scope(&main_arena_);
    CallStop(record);
  }

  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.instance == nullptr) {

      if (record.module != nullptr) Unload(record);
      continue;
    }
    if (scheduler_ != nullptr && scheduler_->IsExecuting(record.id)) {
      smlog::WriteCriticalf(sm::LogLevel::Warn, record.id.c_str(),
                            "插件未响应退出请求（仍在执行中），保留其模块不做卸载");
      continue;
    }
    CallScope scope(&main_arena_);
    CallExit(record);
    DestroyInstance(record);
    Unload(record);
    SetState(record, PluginState::Unloaded, nullptr);
  }

  const std::size_t dropped = post_queue_.Clear();
  if (dropped > 0) {
    smlog::Writef(sm::LogLevel::Warn, "plugin", "退出时丢弃了 %zu 个未执行的主线程任务",
                  dropped);
  }
}

void PluginManager::BeginShutdown() { shutting_down_.store(true); }

void PluginManager::SetState(Record& record, PluginState state, const char* reason,
                             bool crashed) {
  record.state = state;
  record.reason = (reason != nullptr) ? reason : "";
  if (events_ != nullptr && !shutting_down_.load()) {
    events_->OnPluginStateChanged(record.id, state, reason, crashed);
  }
}

void PluginManager::CallStop(Record& record) {
  if (record.instance == nullptr || !record.stop_pending) return;
  record.stop_pending = false;

  CallScope scope(&main_arena_);
  InstanceCall call{record.instance};
  const guard::Outcome outcome = guard::Invoke(&TrampolineStop, &call);
  if (outcome.crashed()) {
    LogCrashWhileUnloading(record.id, "stop", record.auto_disabled, outcome);
  }
}

void PluginManager::CallExit(Record& record) {
  if (record.instance == nullptr || !record.exit_pending) return;
  record.exit_pending = false;

  CallScope scope(&main_arena_);
  InstanceCall call{record.instance};
  const guard::Outcome outcome = guard::Invoke(&TrampolineExit, &call);
  if (outcome.crashed()) {
    LogCrashWhileUnloading(record.id, "exit", record.auto_disabled, outcome);
  }
}

void PluginManager::DestroyInstance(Record& record) {
  if (record.instance == nullptr) return;
  sm::IPlugin* const instance = record.instance;
  record.instance = nullptr;

  if (record.destroy_fn != nullptr) {
    CallScope scope(&main_arena_);
    DestroyCall call{record.destroy_fn, instance};
    const guard::Outcome outcome = guard::Invoke(&TrampolineDestroy, &call);
    if (outcome.crashed()) {
      LogCrashWhileUnloading(record.id, "sm_destroy_plugin", record.auto_disabled, outcome);
    }
  } else {
    smlog::WriteCriticalf(sm::LogLevel::Error, record.id.c_str(),
                          "插件没有导出 sm_destroy_plugin，实例无法安全销毁"
                          "（不跨模块 delete，宁可泄漏一个对象）");
  }
  record.host.reset();
}

void PluginManager::Unload(Record& record) {
  if (record.module != nullptr) {
    platform::unload_module(record.module);
    record.module = nullptr;
  }
  record.meta = nullptr;
  record.subscriptions.clear();
  record.instance = nullptr;
  record.exit_pending = false;
  record.stop_pending = false;
}

void PluginManager::DisablePlugin(const std::string& id, const char* reason,
                                  bool crashed) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->pending_teardown) return;

  record->auto_disabled = true;
  record->enabled = false;
  record->skip_stop = crashed;
  record->pending_teardown = true;

  config_.SetPluginEnabled(record->id, false);
  config_.FlushNow();

  smlog::WriteCriticalf(sm::LogLevel::Error, record->id.c_str(), "插件被自动禁用：%s",
                        (reason != nullptr && *reason != '\0') ? reason : "(未说明原因)");
  SetState(*record, PluginState::Disabled, reason, crashed);
  if (scheduler_ != nullptr) scheduler_->Remove(record->id);

  FinishTeardown(*record);
}

bool PluginManager::FinishTeardown(Record& record) {
  if (record.instance == nullptr) {
    record.pending_teardown = false;
    return true;
  }
  if (scheduler_ != nullptr && scheduler_->IsExecuting(record.id)) return false;

  CallScope scope(&main_arena_);
  if (!record.skip_stop) CallStop(record);
  CallExit(record);
  DestroyInstance(record);
  Unload(record);
  record.pending_teardown = false;
  return true;
}

StartCall PluginManager::RunStart(const std::string& id) {
  StartCall result;
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr) {
    result.result = sm::StartResult::Error;
    result.error = "插件当前没有可用的实例";
    return result;
  }

  record->last_start_unix_ms.store(util::now_unix_ms());

  CallScope scope(&sched_arena_);
  StartCallData call{record->instance, sm::StartResult::Empty, sm::StartOutput{}};
  const guard::Outcome outcome = guard::Invoke(&TrampolineStart, &call);
  if (outcome.crashed()) {
    EndCallFailed(id, "start", outcome);
    result.crashed = true;
    result.result = sm::StartResult::Abort;
    result.error = "start() 崩溃";
    record->last_start_result.store(static_cast<std::int32_t>(result.result));
    record->last_start_crashed.store(true);
    return result;
  }

  result.result = call.result;
  record->started_once.store(true);
  if (result.result == sm::StartResult::Ok) {
    result.items = call.out.items;
    result.error = call.out.error;
    if (result.items.data == nullptr || result.items.size == 0) {
      smlog::Writef(sm::LogLevel::Warn, id.c_str(), "start() 返回 Ok 却没有数据，按 Empty 处理");
      result.result = sm::StartResult::Empty;
      result.items = {};
      result.error = nullptr;
    }
  } else {
    result.error = call.out.error;
    if ((result.error == nullptr || result.error[0] == '\0') &&
        result.result == sm::StartResult::Error && record->host != nullptr) {
      result.error_fallback = record->host->LastErrorText();
    }
    result.items = {};
  }
  record->last_start_result.store(static_cast<std::int32_t>(result.result));
  record->last_start_crashed.store(false);
  return result;
}

bool PluginManager::InvokeOnNotify(const std::string& id, const sm::Notification& notification) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr) return false;

  record->last_notify_unix_ms = util::now_unix_ms();

  CallScope scope(&main_arena_);
  NotifyCall call{record->instance, &notification};
  const guard::Outcome outcome = guard::Invoke(&TrampolineNotify, &call);
  if (outcome.crashed()) {
    record->last_notify_crashed = true;
    EndCallFailed(id, "on_notify", outcome);
    return false;
  }
  record->last_notify_crashed = false;

  if (record->state == PluginState::Initialized) {
    SetState(*record, PluginState::Running, nullptr);
  }
  return true;
}

bool PluginManager::Deliver(const std::string& notifier_id, const sm::Notification& n) {
  return InvokeOnNotify(notifier_id, n);
}

bool PluginManager::InvokeOnAction(const std::string& id, const sm::Notification& notification,
                                   const char* action_id) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr) return false;

  CallScope scope(&main_arena_);
  ActionCall call{record->instance, &notification, action_id};
  const guard::Outcome outcome = guard::Invoke(&TrampolineAction, &call);
  if (outcome.crashed()) {
    EndCallFailed(id, "on_action", outcome);
    return false;
  }
  return true;
}

bool PluginManager::InvokeDrawUi(const std::string& id, sm::Span<const sm::Notification> history) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr) return false;

  CallScope scope(&main_arena_);
  DrawUiCall call{record->instance, history};
  const guard::Outcome outcome = guard::Invoke(&TrampolineDrawUi, &call);
  if (outcome.crashed()) {
    EndCallFailed(id, "draw_ui", outcome);
    return false;
  }
  return true;
}

bool PluginManager::InvokeDrawView(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr) return false;

  CallScope scope(&main_arena_);
  InstanceCall call{record->instance};
  const guard::Outcome outcome = guard::Invoke(&TrampolineDrawView, &call);
  if (outcome.crashed()) {
    EndCallFailed(id, "draw_view", outcome);
    return false;
  }
  return true;
}

void PluginManager::EndCallFailed(const std::string& id, const char* method,
                                  const guard::Outcome& outcome) {
  guard::ReportCrash(id.c_str(), method, outcome);

  const char* const text = guard::ExceptionText(outcome.exception_code);
  char reason[192];
  std::snprintf(reason, sizeof(reason), "%s() 崩溃（异常码 0x%08X%s%s）", method,
                static_cast<unsigned>(outcome.exception_code), text != nullptr ? "，" : "",
                text != nullptr ? text : "");
  DisablePlugin(id, reason, true);
}

std::size_t PluginManager::PumpMainThread() {
  for (auto& holder : records_) {
    if (holder->started_once.exchange(false) && holder->state == PluginState::Initialized) {
      SetState(*holder, PluginState::Running, nullptr);
    }
  }

  for (auto& holder : records_) {
    if (holder->pending_teardown && FinishTeardown(*holder)) {
      smlog::Writef(sm::LogLevel::Info, holder->id.c_str(), "插件已卸载");
    }
  }

  std::size_t executed = 0;
  MainThreadQueue::Item item;
  while (post_queue_.Pop(&item)) {
    Record* const record = FindRecord(item.plugin_id);
    if (record == nullptr || record->instance == nullptr || record->auto_disabled) continue;

    CallScope scope(&main_arena_);
    TaskCall call{item.fn, item.user};
    const guard::Outcome outcome = guard::Invoke(&TrampolineTask, &call);
    if (outcome.crashed()) {
      EndCallFailed(item.plugin_id, "主线程任务", outcome);
    }
    ++executed;
  }
  return executed;
}

std::vector<PluginInfo> PluginManager::Snapshot() const {
  std::vector<PluginInfo> result;
  result.reserve(records_.size());

  for (const auto& holder : records_) {
    const Record& record = *holder;
    PluginInfo info;
    info.id = record.id;
    info.path = record.path;
    info.state = record.state;
    info.reason = record.reason;
    info.enabled = record.enabled;
    info.auto_disabled = record.auto_disabled;
    info.last_start_unix_ms = record.last_start_unix_ms.load();
    info.last_start_result =
        static_cast<sm::StartResult>(record.last_start_result.load());
    info.last_start_crashed = record.last_start_crashed.load();
    info.last_notify_unix_ms = record.last_notify_unix_ms;
    info.last_notify_crashed = record.last_notify_crashed;
    info.name = record.meta_name;
    info.author = record.meta_author;
    info.version = record.meta_version;
    info.description = record.meta_description;
    info.kind = record.meta_kind;
    info.host_range = record.meta_host_range;
    info.setting_count = record.meta_setting_count;
    if (record.meta != nullptr) {
      info.settings = record.meta->settings;
      info.subscriptions =
          sm::Span<const char*>{record.subscriptions.data(), record.subscriptions.size()};
    }
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<NotifierSubscriptions> PluginManager::ActiveNotifierSubscriptions() const {
  std::vector<NotifierSubscriptions> result;

  for (const auto& holder : records_) {
    const Record& record = *holder;
    if (record.meta == nullptr) continue;
    if (record.meta->kind != sm::PluginKind::Notifier) continue;
    if (record.state != PluginState::Loaded &&
        record.state != PluginState::Initialized &&
        record.state != PluginState::Running) {
      continue;
    }
    NotifierSubscriptions decl;
    decl.id = record.id;
    decl.declared = sm::Span<const char*>{record.subscriptions.data(),
                                          record.subscriptions.size()};
    result.push_back(std::move(decl));
  }
  return result;
}

PluginManager::Stats PluginManager::SnapshotStats() const {
  Stats stats;
  for (const auto& holder : records_) {
    const Record& record = *holder;
    ++stats.scanned;
    if (record.meta != nullptr) ++stats.accepted;
    if (record.state == PluginState::Rejected) ++stats.rejected;
    if (record.enabled && record.meta != nullptr) ++stats.enabled;
    if (record.instance != nullptr) ++stats.loaded;
    if (record.state == PluginState::Running) ++stats.running;
    if (record.state == PluginState::Stopped) ++stats.stopped;
    if (record.state == PluginState::Disabled) ++stats.disabled;
    if (record.state == PluginState::InitializeFailed) ++stats.initialize_failed;
  }
  return stats;
}

PluginState PluginManager::StateOf(const std::string& id) const {
  const Record* const record = FindRecord(id);
  return record != nullptr ? record->state : PluginState::Unloaded;
}

sm::IPlugin* PluginManager::InstanceOf(const std::string& id) const {
  const Record* const record = FindRecord(id);
  return record != nullptr ? record->instance : nullptr;
}

bool PluginManager::HasPlugin(const std::string& id) const { return FindRecord(id) != nullptr; }

}  // namespace host
}  // namespace sm

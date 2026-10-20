// SimpleMessage — 插件管理器的实现
#include "host/plugin_manager.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

#include "sm/version.h"
#include "sm/version_range.h"

#include "host/log.h"
#include "host/plugin_id.h"
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

std::string LowerAscii(const std::string& text) {
  std::string result = text;
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return result;
}

// 共享依赖不是插件。它们正常放在 exe 目录，但用户也可能顺手拷进插件目录，
// 别让它们以"缺少导出符号"的形式污染统计。
bool IsSharedDependency(const std::string& name) {
  return EqualsNoCase(name, "sm_imgui.dll") || EqualsNoCase(name, "sm_curl.dll");
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

std::string TrimAsciiWhitespace(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                         text[begin] == '\n')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool ValidateInstanceName(const std::string& name, std::string* error) {
  if (name.size() > kMaxInstanceNameBytes) {
    *error = Fmt("实例名称过长（%zu 字节，上限 %zu）", name.size(), kMaxInstanceNameBytes);
    return false;
  }
  for (char c : name) {
    if (static_cast<unsigned char>(c) < 0x20u || c == 0x7F) {
      *error = "实例名称含有控制字符";
      return false;
    }
  }
  return true;
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

// 一个插件 DLL。模块只加载/校验一次，被该类型的所有实例共享。
struct PluginManager::PluginType {
  std::string file_stem;  // DLL 文件名（不含扩展名），用于“文件名与 meta.id 一致”校验
  std::string id;         // 规范类型 id（meta.id）
  std::string path;
  PluginState state = PluginState::Discovered;
  std::string reason;
  bool        valid = false;

  sm::platform::IModule* module = nullptr;
  const sm::PluginMeta*  meta = nullptr;
  CreatePluginFn         create_fn = nullptr;
  DestroyPluginFn        destroy_fn = nullptr;
  std::vector<const char*> subscriptions;

  std::string    name;
  std::string    version;
  std::string    author;
  std::string    description;
  std::string    host_range;
  sm::PluginKind kind = sm::PluginKind::Source;
  std::size_t    setting_count = 0;
};

// 一个运行实例。id 是实例 id；类型相关的信息全部通过 type 取。
struct PluginManager::Record {
  std::string id;
  PluginType* type = nullptr;
  std::string custom_name;

  PluginState state = PluginState::Discovered;
  std::string reason;

  bool enabled = true;
  bool auto_disabled = false;
  bool pending_teardown = false;
  // 调度线程会读它做快速跳过；主线程写，所以用 atomic 消除数据竞争。
  std::atomic<bool> pending_removal{false};
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

  sm::IPlugin*            instance = nullptr;
  std::unique_ptr<PluginHost> host;
  config::PluginSettings*     settings = nullptr;
};

PluginManager::PluginManager(config::ConfigStore& config)
    : config_(config), main_thread_id_(std::this_thread::get_id()) {}

PluginManager::~PluginManager() {
  for (auto& holder : types_) {
    if (holder->module == nullptr) continue;
    smlog::Writef(sm::LogLevel::Warn, holder->id.c_str(),
                  "插件模块未被显式卸载，在插件管理器析构时释放");
    platform::unload_module(holder->module);
    holder->module = nullptr;
  }
}

void PluginManager::SetScheduleSink(IScheduleSink* sink) {
  scheduler_ = sink;
}

void PluginManager::SetUiSession(IUiSession* session) { ui_ = session; }
void PluginManager::SetIconProvider(IIconProvider* icons) { icons_ = icons; }
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
  setup.env.icons = icons_;
  setup.env.actions = actions_;
  setup.env.shutting_down = &shutting_down_;
  setup.post_queue = &post_queue_;
  return setup;
}

PluginManager::PluginType* PluginManager::AddType(const std::string& file_stem,
                                                  const std::string& path) {
  auto type = std::make_unique<PluginType>();
  type->file_stem = file_stem;
  type->id = file_stem;
  type->path = path;

  PluginType* const raw = type.get();
  types_.push_back(std::move(type));
  {
    std::lock_guard<std::mutex> lock(index_mutex_);
    type_index_[file_stem] = raw;
  }
  return raw;
}

PluginManager::PluginType* PluginManager::FindType(const std::string& type_id) const {
  std::lock_guard<std::mutex> lock(index_mutex_);
  const auto it = type_index_.find(type_id);
  return it != type_index_.end() ? it->second : nullptr;
}

std::string PluginManager::GenerateInstanceId(const PluginType& type) const {
  std::set<std::string> used;
  for (const config::InstanceConfig& instance : config_.Instances()) {
    used.insert(instance.id);
  }
  // 延迟删除中的记录仍占着 id，不能立刻复用。
  for (const auto& holder : records_) used.insert(holder->id);
  return host::NextInstanceId(type.id, used);
}

void PluginManager::UnloadTypeModule(PluginType& type) {
  if (type.module != nullptr) {
    platform::unload_module(type.module);
    type.module = nullptr;
  }
  // 这些指针都指向 DLL 内存，模块卸载后必须立刻清空。
  type.meta = nullptr;
  type.create_fn = nullptr;
  type.destroy_fn = nullptr;
  type.subscriptions.clear();
}

void PluginManager::MaybeUnloadType(PluginType& type) {
  // 只要还有实例记录就保留模块：禁用/初始化失败的实例仍要在界面上展示设置项，
  // 重新启用时也不必重新加载 DLL。最后一条记录被删除后才会真正卸载。
  for (const auto& holder : records_) {
    if (holder->type == &type) return;
  }
  UnloadTypeModule(type);
}

void PluginManager::RejectType(PluginType& type, const std::string& reason) {
  smlog::Writef(sm::LogLevel::Warn, type.id.c_str(), "插件被拒绝：%s（文件 %s）",
                reason.c_str(), type.path.c_str());

  type.create_fn = nullptr;
  type.destroy_fn = nullptr;
  if (type.module != nullptr) {
    platform::unload_module(type.module);
    type.module = nullptr;
  }
  type.meta = nullptr;
  type.subscriptions.clear();
  type.valid = false;
  type.state = PluginState::Rejected;
  type.reason = reason;
}

void PluginManager::TryLoadAndValidateType(PluginType& type) {
  type.state = PluginState::Discovered;
  type.reason.clear();
  type.valid = false;

  const char* load_error = nullptr;
  type.module = platform::load_module(type.path.c_str(), &load_error);
  if (type.module == nullptr) {
    RejectType(type, Fmt("加载失败：%s",
                         load_error != nullptr ? load_error : "未知原因"));
    return;
  }

  const AbiVersionFn    abi_fn = FetchSymbol<AbiVersionFn>(type.module, "sm_abi_version");
  const PluginMetaFn    meta_fn = FetchSymbol<PluginMetaFn>(type.module, "sm_plugin_meta");
  const CreatePluginFn  create_fn = FetchSymbol<CreatePluginFn>(type.module, "sm_create_plugin");
  const DestroyPluginFn destroy_fn =
      FetchSymbol<DestroyPluginFn>(type.module, "sm_destroy_plugin");

  std::string missing;
  if (abi_fn == nullptr) missing += " sm_abi_version";
  if (meta_fn == nullptr) missing += " sm_plugin_meta";
  if (create_fn == nullptr) missing += " sm_create_plugin";
  if (destroy_fn == nullptr) missing += " sm_destroy_plugin";
  if (!missing.empty()) {
    RejectType(type, "缺少导出符号：" + missing);
    return;
  }
  type.create_fn = create_fn;
  type.destroy_fn = destroy_fn;

  AbiCall abi_call{abi_fn, 0};
  const guard::Outcome abi_outcome = guard::Invoke(&TrampolineAbiVersion, &abi_call);
  if (abi_outcome.crashed()) {
    guard::ReportCrash(type.id.c_str(), "sm_abi_version", abi_outcome);
    RejectType(type, "读取 ABI 版本时崩溃");
    return;
  }
  if (abi_call.value != SM_API_VERSION) {
    RejectType(type, Fmt("ABI 版本不匹配：插件是 %d，宿主是 %d", abi_call.value,
                         static_cast<int>(SM_API_VERSION)));
    return;
  }

  MetaCall meta_call{meta_fn, nullptr};
  const guard::Outcome meta_outcome = guard::Invoke(&TrampolinePluginMeta, &meta_call);
  if (meta_outcome.crashed()) {
    guard::ReportCrash(type.id.c_str(), "sm_plugin_meta", meta_outcome);
    RejectType(type, "读取元信息时崩溃");
    return;
  }
  if (meta_call.value == nullptr) {
    RejectType(type, "sm_plugin_meta() 返回空指针");
    return;
  }

  const ValidationResult validation = Validate(type, *meta_call.value);
  if (!validation.ok) {
    RejectType(type, validation.reason);
    return;
  }

  type.meta = meta_call.value;
  type.subscriptions = std::move(validation.subscriptions);

  const std::string canonical_id = type.meta->id;
  if (canonical_id != type.id) {
    std::lock_guard<std::mutex> lock(index_mutex_);
    type_index_.erase(type.id);
    type.id = canonical_id;
    type_index_[type.id] = &type;
  }

  type.name = (type.meta->name != nullptr) ? type.meta->name : type.id;
  type.version = (type.meta->version != nullptr) ? type.meta->version : "";
  type.author = (type.meta->author != nullptr) ? type.meta->author : "";
  type.description =
      (type.meta->description != nullptr) ? type.meta->description : "";
  type.host_range =
      (type.meta->host_range != nullptr) ? type.meta->host_range : "";
  type.kind = type.meta->kind;
  type.setting_count = type.meta->settings.size;
  type.valid = true;
  type.state = PluginState::Discovered;
}

bool PluginManager::EnsureTypeLoaded(PluginType& type) {
  if (type.module != nullptr && type.valid) return true;
  TryLoadAndValidateType(type);
  return type.valid;
}

void PluginManager::SynchronizeInstances() {
  // 1) 为“零实例且没有墓碑”的类型自动补 canonical 实例。判断依据必须是
  //    “该类型一个实例都没有”，而不是“canonical 实例不存在”—— 否则用户
  //    删掉首个实例但保留 @2 时，canonical 会被复活。
  for (auto& holder : types_) {
    PluginType& type = *holder;
    if (!type.valid) continue;
    if (config_.CountInstancesOfType(type.id) != 0) {
      if (config_.IsTypeRemoved(type.id)) config_.ClearTypeRemoved(type.id);
      continue;
    }
    if (config_.IsTypeRemoved(type.id)) continue;

    config::Diagnostics diagnostics;
    const bool added =
        config_.AddInstance(config::InstanceConfig{type.id, type.id, "", true},
                            &diagnostics);
    if (!added && !config_.read_only()) {
      LogConfigDiagnostics(type.id, diagnostics);
    }
  }

  // 2) 为配置里的每条实例建记录。未知类型只告警、保留配置，等插件装回来。
  for (const config::InstanceConfig& instance : config_.Instances()) {
    PluginType* const type = FindType(instance.plugin);
    if (type == nullptr) {
      smlog::Writef(sm::LogLevel::Warn, "plugin",
                    "实例 %s 引用的插件类型 %s 未安装，已跳过",
                    instance.id.c_str(), instance.plugin.c_str());
      continue;
    }
    if (!type->valid) {
      smlog::Writef(sm::LogLevel::Warn, "plugin",
                    "实例 %s 的插件类型 %s 未能加载，已跳过",
                    instance.id.c_str(), instance.plugin.c_str());
      continue;
    }
    // 运行时重新扫描会再次走到这里：已有记录的实例不能重复登记。
    if (FindRecord(instance.id) != nullptr) continue;

    Record* const record = AddRecord(instance.id, *type);
    if (type->kind != sm::PluginKind::Source && instance.id != type->id) {
      record->enabled = false;
      SetState(*record, PluginState::Rejected,
               "Notifier 每种类型只允许一个实例；请从 config.toml 删除这条记录");
    }
  }

  // 3) 只读/配置过新时 AddInstance 不会成功，但界面仍应看到已发现的插件。
  //    这里为还没有记录的类型补一个内存中的 canonical 实例。
  for (auto& holder : types_) {
    PluginType& type = *holder;
    if (!type.valid) continue;
    bool has_record = false;
    for (const auto& record : records_) {
      if (record->type == &type) {
        has_record = true;
        break;
      }
    }
    if (has_record || config_.IsTypeRemoved(type.id)) continue;
    if (config_.CountInstancesOfType(type.id) != 0) continue;
    AddRecord(type.id, type);
  }
}

PluginManager::Record* PluginManager::AddRecord(const std::string& id, PluginType& type) {
  auto record = std::make_unique<Record>();
  record->id = id;
  record->type = &type;

  if (const config::InstanceConfig* instance = config_.FindInstance(id)) {
    record->enabled = instance->enabled;
    record->custom_name = instance->name;
  }

  Record* const raw = record.get();
  records_.push_back(std::move(record));
  {
    std::lock_guard<std::mutex> lock(index_mutex_);
    index_[id] = raw;
  }
  return raw;
}

PluginManager::Record* PluginManager::FindRecord(const std::string& id) const {
  std::lock_guard<std::mutex> lock(index_mutex_);
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
    if (IsSharedDependency(entry.name)) continue;

    const std::string stem = entry.name.substr(0, entry.name.size() - 4);
    PluginType* const type = AddType(stem, util::path_join(plugin_dir, entry.name));
    TryLoadAndValidateType(*type);
  }

  SynchronizeInstances();

  const Stats stats = SnapshotStats();
  smlog::Writef(sm::LogLevel::Info, "plugin",
                "插件目录 %s：扫描到 %d 个类型，通过校验 %d 个，拒绝 %d 个；"
                "实例 %d 个",
                plugin_dir.c_str(), stats.scanned, stats.accepted, stats.rejected,
                static_cast<int>(records_.size()));
}

ReloadResult PluginManager::ReloadPlugins() {
  ReloadResult result;

  if (!discovered_) {
    result.error = "插件目录尚未完成首次扫描";
    return result;
  }
  if (shutting_down_.load()) {
    result.error = "宿主正在退出，不能再加载插件";
    return result;
  }

  // 已知文件按 DLL 文件名（不含扩展名）索引。meta.id 可以换大小写，但文件名
  // 在校验时已经确认与 meta.id 一致，所以这里是稳定的身份。
  std::map<std::string, PluginType*> known;
  for (const auto& holder : types_) {
    known[LowerAscii(holder->file_stem)] = holder.get();
  }

  for (const util::DirEntry& entry : util::list_files(plugin_dir_, nullptr)) {
    if (!EndsWithNoCase(entry.name, ".dll")) continue;
    if (IsSharedDependency(entry.name)) continue;
    ++result.scanned;

    const std::string stem = entry.name.substr(0, entry.name.size() - 4);
    const auto known_it = known.find(LowerAscii(stem));
    if (known_it != known.end()) {
      PluginType& type = *known_it->second;
      if (type.valid) continue;  // 已加载或已登记的类型保持不动
      // 被拒绝的类型当时已经卸载了模块；用户可能已经重新编译并替换了文件。
      TryLoadAndValidateType(type);
      if (type.valid) ++result.recovered;
      continue;
    }

    ++result.added;
    PluginType* const type = AddType(stem, util::path_join(plugin_dir_, entry.name));
    TryLoadAndValidateType(*type);
    if (type->valid) {
      ++result.accepted;
    } else {
      // 校验失败的类型已经由 RejectType() 卸载并记录原因，这里直接跳过。
      ++result.rejected;
    }
  }

  // 新类型可能对上配置里原本"未安装"的实例；SynchronizeInstances 是幂等的。
  const Stats before = SnapshotStats();
  SynchronizeInstances();
  LoadEnabled();
  const Stats after = SnapshotStats();
  result.instances_loaded = after.loaded - before.loaded;
  result.ok = true;

  smlog::Writef(sm::LogLevel::Info, "plugin",
                "插件目录重新扫描：%d 个 DLL，新增 %d 个（通过 %d、拒绝 %d），"
                "恢复 %d 个，新加载实例 %d 个",
                result.scanned, result.added, result.accepted, result.rejected,
                result.recovered, result.instances_loaded);
  return result;
}

PluginManager::ValidationResult PluginManager::Validate(const PluginType& type,
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

  if (!EqualsNoCase(type.file_stem, meta.id)) {
    return {false, Fmt("DLL 文件名 \"%s.dll\" 与 meta.id \"%s\" 不一致",
                       type.file_stem.c_str(), meta.id), {}};
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

  for (const auto& entry : types_) {
    if (entry.get() == &type) continue;
    if (entry->valid && entry->id == meta.id) {
      return {false,
              Fmt("插件 id \"%s\" 与已加载的 %s 重复", meta.id, entry->path.c_str()),
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
        type_value > static_cast<std::int32_t>(sm::SettingType::Sound)) {
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
    smlog::Writef(sm::LogLevel::Warn, type.id.c_str(),
                  "订阅模式声明了数量却没有数据指针，按未声明订阅处理");
  } else {
    if (subscriptions.size > sm::limits::kMaxSubscriptions) {
      smlog::Writef(sm::LogLevel::Warn, type.id.c_str(),
                    "订阅模式过多（%zu 个，上限 %zu），超出的部分被忽略", subscriptions.size,
                    sm::limits::kMaxSubscriptions);
    }
    for (std::size_t i = 0; i < subscriptions.size; ++i) {
      if (result.subscriptions.size() >= sm::limits::kMaxSubscriptions) break;
      const char* const pattern = subscriptions.data[i];
      if (pattern == nullptr || *pattern == '\0') {
        smlog::Writef(sm::LogLevel::Warn, type.id.c_str(),
                      "第 %zu 个订阅模式为空，已跳过", i + 1);
        continue;
      }
      result.subscriptions.push_back(pattern);
    }
  }

  result.ok = true;
  return result;
}

void PluginManager::LoadEnabled() {
  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.state != PluginState::Discovered) continue;
    if (record.pending_removal) continue;
    if (!record.enabled) {
      smlog::Write(sm::LogLevel::Info, record.id.c_str(), "插件未被启用，跳过加载");
      continue;
    }
    if (record.type == nullptr || !EnsureTypeLoaded(*record.type)) {
      const char* reason =
          (record.type != nullptr && !record.type->reason.empty())
              ? record.type->reason.c_str()
              : "插件类型不可用";
      SetState(record, PluginState::Rejected, reason);
      continue;
    }
    CreateAndInitialize(record);
  }

  // 所有实例都禁用/失败的模块可以释放；重新启用时会重新加载并校验。
  for (auto& holder : types_) MaybeUnloadType(*holder);

  const Stats stats = SnapshotStats();
  smlog::Writef(sm::LogLevel::Info, "plugin",
                "插件加载完成：启用 %d 个实例、创建实例 %d 个、运行中 %d 个、"
                "初始化失败 %d 个",
                stats.enabled, stats.loaded, stats.running, stats.initialize_failed);
}

void PluginManager::CreateAndInitialize(Record& record) {
  if (record.type == nullptr || record.type->meta == nullptr) {
    SetState(record, PluginState::InitializeFailed, "插件类型未加载");
    return;
  }

  config::Diagnostics diagnostics;
  record.settings = &config_.EnsurePluginSettings(record.id, record.type->meta->settings,
                                                  &diagnostics);
  LogConfigDiagnostics(record.id, diagnostics);

  record.host = std::make_unique<PluginHost>(MakeHostSetup(record));

  if (record.type->create_fn == nullptr) {
    SetState(record, PluginState::InitializeFailed, "插件没有导出 sm_create_plugin");
    record.host.reset();
    record.settings = nullptr;
    return;
  }

  CallScope scope(&main_arena_);
  CreateCall call{record.type->create_fn, record.host.get(), nullptr};
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

  if (record.type != nullptr && record.type->kind == sm::PluginKind::Source &&
      scheduler_ != nullptr) {
    scheduler_->Add(record.id);
  }
  return true;
}

bool PluginManager::Enable(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->pending_removal) return false;
  // 自动禁用的 stop/exit 尚未完成时不能重建实例：那会和调度线程上的
  // start() 并发，也会覆盖 pending_teardown 的收尾状态。
  if (record->pending_teardown) return false;
  if (scheduler_ != nullptr && scheduler_->IsExecuting(record->id)) return false;

  switch (record->state) {
    case PluginState::Discovered:
    case PluginState::Stopped:
    case PluginState::InitializeFailed:
    case PluginState::Disabled:
      break;
    default:
      return false;
  }

  config_.SetInstanceEnabled(record->id, true);
  record->enabled = true;
  record->auto_disabled = false;

  if (record->type == nullptr) return false;
  if (!EnsureTypeLoaded(*record->type)) {
    SetState(*record, PluginState::Rejected, record->type->reason.c_str());
    return false;
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
  if (record == nullptr || record->pending_removal) return false;

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
  config_.SetInstanceEnabled(record->id, false);
  record->auto_disabled = false;
  record->skip_stop = false;

  if (record->instance != nullptr &&
      !(scheduler_ != nullptr && scheduler_->IsExecuting(record->id))) {
    CallScope scope(&main_arena_);
    CallStop(*record);
  } else if (record->instance != nullptr) {
    // start() 正在调度线程上执行：不能在这里并发调用 stop()。标记 teardown，
    // 由主线程 PumpMainThread 在 start 返回后收尾。
    record->pending_teardown = true;
  }
  if (scheduler_ != nullptr) scheduler_->Remove(record->id);
  SetState(*record, PluginState::Stopped, nullptr);
  return true;
}

bool PluginManager::RetryInitialize(const std::string& id) {
  Record* const record = FindRecord(id);
  if (record == nullptr || record->pending_removal) return false;
  if (record->state != PluginState::InitializeFailed) return false;
  record->auto_disabled = false;

  if (record->type == nullptr) return false;
  if (record->instance == nullptr) {
    if (!EnsureTypeLoaded(*record->type)) {
      SetState(*record, PluginState::Rejected, record->type->reason.c_str());
      return false;
    }
    CreateAndInitialize(*record);
  } else {
    InitializeInstance(*record);
  }
  return record->state != PluginState::InitializeFailed &&
         record->state != PluginState::Rejected;
}

void PluginManager::ReleaseDiscoveredModules() {
  // 安全模式：显式释放所有 DLL，但保留类型/实例记录供界面展示。
  for (auto& holder : types_) UnloadTypeModule(*holder);
}

AddInstanceResult PluginManager::AddInstance(const std::string& type_id,
                                             const std::string& display_name) {
  AddInstanceResult result;
  if (config_.read_only() || config_.SchemaTooNewDetected()) {
    result.error = "配置当前为只读，无法添加实例";
    return result;
  }

  PluginType* const type = FindType(type_id);
  if (type == nullptr) {
    result.error = "未找到插件类型 " + type_id;
    return result;
  }
  if (!type->valid) {
    result.error = "插件类型 " + type->id + " 不可用：" + type->reason;
    return result;
  }
  if (type->kind != sm::PluginKind::Source) {
    result.error = "只有通知源插件支持多个实例";
    return result;
  }

  const std::string name = TrimAsciiWhitespace(display_name);
  std::string name_error;
  if (!ValidateInstanceName(name, &name_error)) {
    result.error = name_error;
    return result;
  }

  const std::string id = GenerateInstanceId(*type);
  if (id.empty()) {
    result.error = "插件类型 " + type->id + " 已达到实例上限 " +
                   std::to_string(kMaxInstancesPerType);
    return result;
  }

  config::Diagnostics diagnostics;
  if (!config_.AddInstance(config::InstanceConfig{id, type->id, name, true},
                           &diagnostics)) {
    for (const config::Diagnostic& diagnostic : diagnostics) {
      if (diagnostic.severity != config::Diagnostic::Severity::Info) {
        result.error = diagnostic.message;
        break;
      }
    }
    if (result.error.empty()) result.error = "写入配置失败";
    return result;
  }
  config_.FlushNow();

  // 最后一个实例被删掉时模块可能已经卸载；重新加载失败就回滚配置条目。
  if (!EnsureTypeLoaded(*type)) {
    config::Diagnostics rollback;
    config_.RemoveInstance(id, &rollback);
    config_.FlushNow();
    result.error = "插件模块重新加载失败：" +
                   (type->reason.empty() ? std::string("原因未知") : type->reason);
    return result;
  }

  Record* const record = AddRecord(id, *type);
  record->custom_name = name;
  record->enabled = true;
  CreateAndInitialize(*record);

  smlog::Writef(sm::LogLevel::Info, id.c_str(), "已添加 %s 的实例（类型 %s）",
                type->name.c_str(), type->id.c_str());
  result.ok = true;
  result.instance_id = id;
  return result;
}

bool PluginManager::RemoveInstance(const std::string& instance_id,
                                   std::string* out_error) {
  const auto fail = [out_error](const char* text) {
    if (out_error != nullptr) *out_error = text;
    return false;
  };

  Record* const record = FindRecord(instance_id);
  if (record == nullptr) return fail("实例不存在");
  if (record->type == nullptr) return fail("实例没有关联的插件类型");
  if (record->type->kind != sm::PluginKind::Source) {
    return fail("只有通知源插件支持删除实例");
  }
  if (record->pending_removal) return fail("实例正在删除");
  if (config_.read_only() || config_.SchemaTooNewDetected()) {
    return fail("配置当前为只读，无法删除实例");
  }

  // 先落盘再从内存摘除：崩溃最多留下孤儿设置文件，不会留下“配置里有、界面没有”。
  config::Diagnostics diagnostics;
  if (!config_.RemoveInstance(record->id, &diagnostics)) {
    for (const config::Diagnostic& diagnostic : diagnostics) {
      if (diagnostic.severity != config::Diagnostic::Severity::Info) {
        if (out_error != nullptr) *out_error = diagnostic.message;
        break;
      }
    }
    if (out_error != nullptr && out_error->empty()) *out_error = "写入配置失败";
    return false;
  }
  config_.FlushNow();

  record->enabled = false;
  record->pending_removal = true;
  record->skip_stop = false;
  if (scheduler_ != nullptr) scheduler_->Remove(record->id);
  SetState(*record, PluginState::Stopped, nullptr);

  // 没在调度线程里执行时可以立刻收尾；否则留给 PumpMainThread 下一帧重试。
  TryFinishRemoval(*record);
  return true;
}

bool PluginManager::RenameInstance(const std::string& instance_id,
                                   const std::string& display_name) {
  Record* const record = FindRecord(instance_id);
  if (record == nullptr || record->pending_removal) return false;
  if (config_.read_only() || config_.SchemaTooNewDetected()) return false;

  const std::string name = TrimAsciiWhitespace(display_name);
  std::string name_error;
  if (!ValidateInstanceName(name, &name_error)) return false;
  if (!config_.SetInstanceName(record->id, name)) return false;

  record->custom_name = name;
  return true;
}

void PluginManager::ShutdownAll() {
  std::set<PluginType*> blocked;

  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.instance == nullptr) continue;
    if (scheduler_ != nullptr && scheduler_->IsExecuting(record.id)) {
      smlog::WriteCriticalf(sm::LogLevel::Warn, record.id.c_str(),
                            "插件未响应退出请求（仍在执行中），跳过 stop()");
      if (record.type != nullptr) blocked.insert(record.type);
      continue;
    }
    CallScope scope(&main_arena_);
    CallStop(record);
  }

  for (auto& holder : records_) {
    Record& record = *holder;
    if (record.instance == nullptr) continue;
    if (blocked.find(record.type) != blocked.end()) {
      smlog::WriteCriticalf(sm::LogLevel::Warn, record.id.c_str(),
                            "插件未响应退出请求（仍在执行中），保留其模块不做卸载");
      continue;
    }
    CallScope scope(&main_arena_);
    CallExit(record);
    DestroyInstance(record);
    SetState(record, PluginState::Unloaded, nullptr);

    if (record.pending_removal) {
      config_.ForgetPluginSettings(record.id);
      config::Diagnostics diagnostics;
      config_.DeletePluginSettingsFiles(record.id, &diagnostics);
      LogConfigDiagnostics(record.id, diagnostics);
    }
  }

  for (auto& holder : types_) {
    if (blocked.find(holder.get()) != blocked.end()) continue;
    UnloadTypeModule(*holder);
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
  record.exit_pending = false;
  record.stop_pending = false;

  const DestroyPluginFn destroy_fn =
      (record.type != nullptr) ? record.type->destroy_fn : nullptr;
  if (destroy_fn != nullptr) {
    CallScope scope(&main_arena_);
    DestroyCall call{destroy_fn, instance};
    const guard::Outcome outcome = guard::Invoke(&TrampolineDestroy, &call);
    if (outcome.crashed()) {
      LogCrashWhileUnloading(record.id, "sm_destroy_plugin", record.auto_disabled, outcome);
    }
  } else {
    smlog::WriteCriticalf(sm::LogLevel::Error, record.id.c_str(),
                          "插件没有导出 sm_destroy_plugin，实例无法安全销毁"
                          "（不跨模块 delete，宁可泄漏一个对象）");
  }
  // PluginHost 持有 PluginSettings*，必须随实例一起销毁。
  record.host.reset();
  record.settings = nullptr;
}

void PluginManager::DisablePlugin(const std::string& id, const char* reason,
                                  bool crashed) {
  if (std::this_thread::get_id() != main_thread_id_) {
    // 调度线程（start 崩溃 / Abort）不能碰 ConfigStore、Router 与 UI 状态：
    // 入队交给主线程，RunStart 返回后也不会再调度该 id。
    std::lock_guard<std::mutex> lock(pending_disable_mutex_);
    pending_disables_.push_back(
        PendingDisable{id, reason != nullptr ? reason : "", crashed});
    return;
  }

  Record* const record = FindRecord(id);
  if (record == nullptr || record->pending_teardown || record->pending_removal) return;

  record->auto_disabled = true;
  record->enabled = false;
  record->skip_stop = crashed;
  record->pending_teardown = true;

  config_.SetInstanceEnabled(record->id, false);
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
  record.pending_teardown = false;
  // 崩溃自动禁用后实例已销毁，没有兄弟实例时模块可以释放（重新启用会重新加载）。
  if (record.type != nullptr) MaybeUnloadType(*record.type);
  return true;
}

bool PluginManager::TryFinishRemoval(Record& record) {
  if (!record.pending_removal) return false;
  // 调度线程可能正在执行 start()；等它返回后再销毁，期间记录仍然占着 id。
  if (record.instance != nullptr && scheduler_ != nullptr &&
      scheduler_->IsExecuting(record.id)) {
    return false;
  }

  CallScope scope(&main_arena_);
  CallStop(record);
  CallExit(record);
  DestroyInstance(record);

  // 顺序很重要：插件对象（持有 PluginSettings*）先销毁，再忘掉内存条目、删文件。
  config_.ForgetPluginSettings(record.id);
  config::Diagnostics diagnostics;
  config_.DeletePluginSettingsFiles(record.id, &diagnostics);
  LogConfigDiagnostics(record.id, diagnostics);

  PluginType* const type = record.type;
  // EraseRecord 会销毁 record 对象：先把日志要用的 id 复制出来。
  const std::string id = record.id;
  SetState(record, PluginState::Unloaded, nullptr);
  EraseRecord(record);
  smlog::Writef(sm::LogLevel::Info, type != nullptr ? type->id.c_str() : "plugin",
                "实例 %s 已删除", id.c_str());
  if (type != nullptr) MaybeUnloadType(*type);
  return true;
}

void PluginManager::EraseRecord(Record& record) {
  {
    std::lock_guard<std::mutex> lock(index_mutex_);
    index_.erase(record.id);
  }
  for (auto it = records_.begin(); it != records_.end(); ++it) {
    if (it->get() != &record) continue;
    records_.erase(it);
    return;
  }
}

StartCall PluginManager::RunStart(const std::string& id) {
  StartCall result;
  Record* const record = FindRecord(id);
  if (record == nullptr || record->instance == nullptr || record->pending_removal) {
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
  if (record == nullptr || record->instance == nullptr || record->pending_removal) return false;

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
  if (record == nullptr || record->instance == nullptr || record->pending_removal) return false;

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

void PluginManager::ApplyPendingDisables() {
  std::vector<PendingDisable> pending;
  {
    std::lock_guard<std::mutex> lock(pending_disable_mutex_);
    pending.swap(pending_disables_);
  }
  for (const PendingDisable& item : pending) {
    DisablePlugin(item.id, item.reason.c_str(), item.crashed);
  }
}

std::size_t PluginManager::PumpMainThread() {
  ApplyPendingDisables();

  for (auto& holder : records_) {
    if (holder->started_once.exchange(false) && holder->state == PluginState::Initialized) {
      SetState(*holder, PluginState::Running, nullptr);
    }
  }

  for (auto& holder : records_) {
    if (holder->pending_teardown && !holder->pending_removal &&
        FinishTeardown(*holder)) {
      smlog::Writef(sm::LogLevel::Info, holder->id.c_str(), "插件已卸载");
    }
  }

  // 删除收尾会把记录从 records_ 移除；先收集指针，再逐个尝试。
  std::vector<Record*> removals;
  for (auto& holder : records_) {
    if (holder->pending_removal) removals.push_back(holder.get());
  }
  for (Record* const record : removals) {
    if (record->pending_removal) TryFinishRemoval(*record);
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
  result.reserve(records_.size() + types_.size());

  // 一次遍历统计每种类型的存活实例数。Snapshot() 是 const 且每帧调用，
  // 不能像以前那样为每条记录回扫 records_，也不能修改 PluginType。
  std::map<const PluginType*, std::size_t> alive_counts;
  for (const auto& holder : records_) {
    if (holder->type != nullptr && !holder->pending_removal) {
      ++alive_counts[holder->type];
    }
  }

  const auto fill_type_metadata = [](const PluginType& type, std::size_t instance_count,
                                     PluginInfo* info) {
    info->type_id = type.id;
    info->type_name = type.name.empty() ? type.id : type.name;
    info->path = type.path;
    info->host_range = type.host_range;
    info->kind = type.kind;
    info->version = type.version;
    info->author = type.author;
    info->description = type.description;
    info->setting_count = type.setting_count;
    if (type.meta != nullptr) info->settings = type.meta->settings;
    info->subscriptions =
        sm::Span<const char*>{type.subscriptions.data(), type.subscriptions.size()};
    info->can_add_instance = type.valid && type.kind == sm::PluginKind::Source &&
                             instance_count < kMaxInstancesPerType;
  };

  for (const auto& holder : records_) {
    const Record& record = *holder;
    if (record.pending_removal) continue;

    PluginType* const type = record.type;
    PluginInfo info;
    info.id = record.id;
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
    info.custom_name = record.custom_name;
    info.is_instance = true;

    if (type != nullptr) {
      const auto count_it = alive_counts.find(type);
      info.instance_count =
          count_it != alive_counts.end() ? count_it->second : 0;
      info.instance_ordinal = static_cast<std::size_t>(
          std::max(1, InstanceOrdinal(record.id, type->id)));
      fill_type_metadata(*type, info.instance_count, &info);
      info.can_delete_instance = type->valid && type->kind == sm::PluginKind::Source;
    }
    info.settings_path = util::path_join(config_.PathsRef().plugin_config_dir,
                                         record.id + ".toml");

    if (!info.custom_name.empty()) {
      info.name = info.custom_name;
    } else if (info.instance_ordinal <= 1) {
      info.name = info.type_name;
    } else {
      info.name = Fmt("%s #%zu", info.type_name.c_str(), info.instance_ordinal);
    }
    result.push_back(std::move(info));
  }

  // 没有任何运行记录的类型也要出现在界面上：被拒绝的类型给出原因，
  // 被用户删空的类型仍提供“添加实例”入口。
  for (const auto& holder : types_) {
    PluginType& type = *holder;
    const auto count_it = alive_counts.find(&type);
    const std::size_t count = count_it != alive_counts.end() ? count_it->second : 0;
    if (count != 0) continue;

    PluginInfo info;
    info.id = type.id;
    info.name = type.name.empty() ? type.id : type.name;
    info.reason = type.reason;
    info.is_instance = false;
    fill_type_metadata(type, count, &info);

    if (!type.valid) {
      info.state = PluginState::Rejected;
      info.is_rejected_type = true;
      info.enabled = false;
      info.can_add_instance = false;
    } else {
      info.state = PluginState::Discovered;
      info.settings_path.clear();  // 没有实例，也就没有设置文件
    }
    result.push_back(std::move(info));
  }

  return result;
}

std::vector<PluginTypeInfo> PluginManager::SnapshotTypes() const {
  std::vector<PluginTypeInfo> result;
  result.reserve(types_.size());

  std::map<const PluginType*, std::size_t> alive_counts;
  for (const auto& holder : records_) {
    if (holder->type != nullptr && !holder->pending_removal) {
      ++alive_counts[holder->type];
    }
  }

  for (const auto& holder : types_) {
    const PluginType& type = *holder;
    const auto count_it = alive_counts.find(&type);
    const std::size_t count = count_it != alive_counts.end() ? count_it->second : 0;

    PluginTypeInfo info;
    info.type_id = type.id;
    info.name = type.name.empty() ? type.id : type.name;
    info.path = type.path;
    info.host_range = type.host_range;
    info.kind = type.kind;
    info.state = type.valid ? PluginState::Discovered : PluginState::Rejected;
    info.reason = type.reason;
    info.instance_count = count;
    info.module_loaded = type.module != nullptr;
    info.can_add_instance = type.valid && type.kind == sm::PluginKind::Source &&
                            count < kMaxInstancesPerType;
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<NotifierSubscriptions> PluginManager::ActiveNotifierSubscriptions() const {
  std::vector<NotifierSubscriptions> result;

  for (const auto& holder : records_) {
    const Record& record = *holder;
    if (record.pending_removal) continue;
    if (record.type == nullptr || !record.type->valid) continue;
    if (record.type->kind != sm::PluginKind::Notifier) continue;
    if (record.state != PluginState::Loaded &&
        record.state != PluginState::Initialized &&
        record.state != PluginState::Running) {
      continue;
    }
    NotifierSubscriptions decl;
    decl.id = record.id;
    decl.declared = sm::Span<const char*>{record.type->subscriptions.data(),
                                          record.type->subscriptions.size()};
    result.push_back(std::move(decl));
  }
  return result;
}

PluginManager::Stats PluginManager::SnapshotStats() const {
  Stats stats;

  for (const auto& holder : types_) {
    const PluginType& type = *holder;
    ++stats.scanned;
    if (type.valid) ++stats.accepted;
    if (type.state == PluginState::Rejected) ++stats.rejected;
  }

  for (const auto& holder : records_) {
    const Record& record = *holder;
    if (record.pending_removal) continue;
    const bool type_ok = record.type != nullptr && record.type->valid;
    if (record.enabled && type_ok) ++stats.enabled;
    if (record.instance != nullptr) ++stats.loaded;
    if (record.state == PluginState::Rejected) ++stats.rejected;
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

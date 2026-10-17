// SimpleMessage — 应用外壳的实现：启动 12 步、消息循环、关停 8 步
#include "host/app.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "host/log.h"
#include "host/monitor.h"
#include "host/strings.h"
#include "host/ui_root.h"
#include "host/util.h"

#include "platform/platform.h"
#include "platform/win32/win32_shared.h"

#include "sm/version.h"

namespace sm {
namespace host {
namespace {

constexpr std::int64_t kIdleWakeMs = 1000;
constexpr std::int64_t kShutdownGraceMs = 5000;

constexpr int kPanelDefaultWidth = 900;
constexpr int kPanelDefaultHeight = 600;
constexpr int kPanelMinWidth = 640;
constexpr int kPanelMinHeight = 400;

constexpr int kNotifyDefaultWidth = 480;
constexpr int kNotifyDefaultHeight = 560;
constexpr int kNotifyMargin = 16;

constexpr int kDetailDefaultWidth = 720;
constexpr int kDetailDefaultHeight = 560;
constexpr int kDetailMinWidth = 420;
constexpr int kDetailMinHeight = 300;
constexpr int kDetailCascadeOffset = 28;

constexpr std::int64_t kNotifyDormantMs = 30000;

void LogAttachedMonitors() {
  const std::vector<MonitorInfo> monitors = EnumerateMonitors();
  if (monitors.empty()) {
    sm::log::Write(sm::LogLevel::Warn, "window",
                   "没有枚举到任何显示器，通知表面按主显示器那一路摆");
    return;
  }
  for (const MonitorInfo& monitor : monitors) {
    sm::log::Writef(sm::LogLevel::Info, "window",
                    "显示器 %s%s %dx%d，工作区 %dx%d 于 (%d,%d)",
                    monitor.device.c_str(), monitor.primary ? "（主）" : "",
                    monitor.full.width, monitor.full.height, monitor.work.width,
                    monitor.work.height, monitor.work.x, monitor.work.y);
  }
}

constexpr int ExitCodeValue(ExitCode code) { return static_cast<int>(code); }

bool g_console_ready = false;

bool StreamWritable(HANDLE handle) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
  const DWORD type = ::GetFileType(handle);
  if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) return true;
  DWORD mode = 0;
  return ::GetConsoleMode(handle, &mode) != 0;
}

void PointStdStreamsAtConsole() {
  FILE* stream = nullptr;
  ::freopen_s(&stream, "CONOUT$", "w", stdout);
  ::freopen_s(&stream, "CONOUT$", "w", stderr);
}

bool EnsureConsoleOutput() {
  if (g_console_ready) return true;
  g_console_ready = true;

  if (StreamWritable(::GetStdHandle(STD_OUTPUT_HANDLE))) {
    ::AttachConsole(ATTACH_PARENT_PROCESS);
    return true;
  }

  if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole()) {
    PointStdStreamsAtConsole();
    return true;
  }
  return false;
}

void WriteToHandle(HANDLE handle, const char* utf8_text) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE || utf8_text == nullptr) {
    return;
  }
  DWORD written = 0;
  DWORD mode = 0;
  if (::GetConsoleMode(handle, &mode)) {
    const std::wstring wide = util::widen(utf8_text);
    ::WriteConsoleW(handle, wide.c_str(), static_cast<DWORD>(wide.size()), &written,
                    nullptr);
    return;
  }
  const std::size_t length = std::strlen(utf8_text);
  ::WriteFile(handle, utf8_text, static_cast<DWORD>(length), &written, nullptr);
}

HANDLE OutputHandle(bool is_error) {
  const DWORD id = is_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
  HANDLE handle = ::GetStdHandle(id);
  if (!StreamWritable(handle)) {
    EnsureConsoleOutput();
    handle = ::GetStdHandle(id);
  }
  return handle;
}

void PrintToStream(bool is_error, const char* utf8_text) {

  platform::emit_debug_output(utf8_text);
  WriteToHandle(OutputHandle(is_error), utf8_text);
}

void PrintToConsole(const char* utf8_text) { PrintToStream(false, utf8_text); }

void PrintErrorToConsole(const char* utf8_text) { PrintToStream(true, utf8_text); }

bool TakeOptionValue(int argc, wchar_t** argv, int* index, const char* arg,
                     std::string* out) {
  const int next = *index + 1;
  if (next >= argc || argv[next] == nullptr || argv[next][0] == L'-') {
    char line[128];
    std::snprintf(line, sizeof(line), strings::kMissingValueFormat, arg);
    PrintErrorToConsole(line);
    return false;
  }
  *index = next;
  *out = util::narrow(argv[next]);
  return true;
}

void EchoLine(const char* utf8_line) {
  if (utf8_line == nullptr) return;
  platform::emit_debug_output(utf8_line);
  if (g_console_ready) WriteToHandle(::GetStdHandle(STD_OUTPUT_HANDLE), utf8_line);
}

bool HasPrefixCI(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) return false;
  for (; *prefix != 0; ++text, ++prefix) {
    if (*text == 0) return false;
    const char a = (*text >= 'A' && *text <= 'Z') ? static_cast<char>(*text + 32) : *text;
    const char b =
        (*prefix >= 'A' && *prefix <= 'Z') ? static_cast<char>(*prefix + 32) : *prefix;
    if (a != b) return false;
  }
  return true;
}

bool SubscriberAccepts(const Subscriber& subscriber, const std::string& topic) {
  if (subscriber.all_topics) return true;
  for (const std::string& pattern : subscriber.patterns) {
    if (TopicMatches(pattern.c_str(), topic)) return true;
  }
  return false;
}

platform::win32::TrayLabels MakeTrayLabels() {
  platform::win32::TrayLabels labels;
  labels.tooltip = strings::kTrayTooltip;
  labels.open_panel = strings::kTrayOpenPanel;
  labels.pause = strings::kTrayPause;
  labels.open_config_dir = strings::kTrayOpenConfigDir;
  labels.open_log_dir = strings::kTrayOpenLogDir;
  labels.about = strings::kTrayAbout;
  labels.exit = strings::kTrayExit;
  return labels;
}

}  // namespace

App::App() : plugins_(config_) {}

App::~App() {
  DestroyAllDetailSurfaces();
  delete tray_;
  delete runtime_;
  delete instance_lock_;

  window_.reset();
  platform::window::Shutdown();
}

int App::Run(int argc, wchar_t** argv) {
  started_ms_ = platform::monotonic_ms();

  if (ParseArgs(argc, argv) == ArgsResult::kHandled) return ExitCodeValue(ExitCode::kOk);

  const ExitCode startup = StartupConfigAndLogging();
  if (startup != ExitCode::kOk) return ExitCodeValue(startup);

  if (!AcquireSingleInstance()) return ExitCodeValue(ExitCode::kOk);

  if (!InitPlatform()) return ExitCodeValue(ExitCode::kPlatform);

  if (!WireSubsystems()) {
    sm::log::WriteCritical(sm::LogLevel::Error, "host", "控制面板未能建立（内存不足？），启动中止");
    return ExitCodeValue(ExitCode::kFatal);
  }

  if (config_.config_was_reset()) {
    internal_source_.PostEvent(HostEvent::ConfigReset, "", strings::kConfigResetDetail);
  }

  plugins_.Discover(paths_.plugin_dir);

  if (safe_mode_) {
    plugins_.ReleaseDiscoveredModules();
  } else {
    plugins_.LoadEnabled();
  }

  RebuildRouting();

  if (config_.SchemaTooNewDetected()) {
    sm::log::WriteCritical(sm::LogLevel::Error, "host", strings::kPluginSchemaTooNewDetail);
    internal_source_.PostEvent(HostEvent::ConfigSchemaTooNew, "",
                               strings::kPluginSchemaTooNewDetail);
    pipeline_.PumpMainThread();
    Shutdown();
    return ExitCodeValue(ExitCode::kConfigTooNew);
  }

  scheduler_.start();

  internal_source_.PostEvent(HostEvent::HostStarted, "", strings::kHostStartedDetail);

  if (safe_mode_ && panel_ != nullptr) panel_->Open();

  RunMessageLoop();

  Shutdown();
  return ExitCodeValue(ExitCode::kOk);
}

App::ArgsResult App::ParseArgs(int argc, wchar_t** argv) {
  for (int i = 1; i < argc && argv != nullptr; ++i) {
    if (argv[i] == nullptr) continue;
    const std::string arg = util::narrow(argv[i]);

    if (arg == "--help" || arg == "-h") {
      PrintToConsole(strings::kUsageText);
      return ArgsResult::kHandled;
    }
    if (arg == "--version") {
      char line[64];
      std::snprintf(line, sizeof(line), "%s\n", SM_VERSION_STRING);
      PrintToConsole(line);
      return ArgsResult::kHandled;
    }
    if (arg == "--console") {
      console_ = true;
      continue;
    }
    if (arg == "--safe-mode") {
      safe_mode_ = true;
      continue;
    }

    if (arg == "--config-dir" || arg == "--data-dir" || arg == "--plugin-dir" ||
        arg == "--renderer") {
      std::string value;
      if (!TakeOptionValue(argc, argv, &i, arg.c_str(), &value)) continue;

      if (arg == "--config-dir") {
        config_dir_arg_ = value;
      } else if (arg == "--data-dir") {
        data_dir_arg_ = value;
      } else if (arg == "--plugin-dir") {
        plugin_dir_arg_ = value;
      } else if (!ParseRendererName(value, &renderer_)) {
        char line[192];
        std::snprintf(line, sizeof(line), strings::kUnknownRendererFormat, value.c_str());
        PrintErrorToConsole(line);
      }
      continue;
    }

    char line[192];
    std::snprintf(line, sizeof(line), strings::kUnknownArgFormat, arg.c_str());
    PrintErrorToConsole(line);
  }
  return ArgsResult::kContinue;
}

ExitCode App::StartupConfigAndLogging() {

  if (console_) {
    if (EnsureConsoleOutput()) {
      sm::log::SetEchoSink(&EchoLine);
    } else {
      sm::log::Write(sm::LogLevel::Warn, "host",
                     "--console 指定的控制台不可用，日志只写文件");
    }
  }

  if (!config_dir_arg_.empty()) overrides_.config_dir = config_dir_arg_.c_str();
  if (!data_dir_arg_.empty()) overrides_.data_dir = data_dir_arg_.c_str();
  if (!plugin_dir_arg_.empty()) overrides_.plugin_dir = plugin_dir_arg_.c_str();
  paths_ = config::resolve_paths(overrides_);

  const config::AppConfig defaults{};
  sm::log::Configure(paths_.log_dir.c_str(), defaults.log_retention_days,
                     defaults.log_max_total_mb);

  config::Diagnostics diagnostics;
  const config::ConfigStore::LoadResult load =
      config_.Load(paths_, safe_mode_, &diagnostics);

  LogConfigDiagnostics(diagnostics);

  if (load == config::ConfigStore::LoadResult::SchemaTooNew) {
    sm::log::WriteCritical(sm::LogLevel::Error, "host",
                           "config.toml 的 schema_version 高于本宿主，拒绝启动（退出码 2）");
    return ExitCode::kConfigTooNew;
  }

  const config::AppConfig& app = config_.App();
  sm::log::Configure(paths_.log_dir.c_str(), app.log_retention_days,
                     app.log_max_total_mb);
  sm::log::SetMinLevel(app.log_level);

  std::string error;
  ui_state_load_ = LoadUiState(UiStatePath(paths_), &ui_state_, &error);
  if (ui_state_load_ == UiStateLoad::Corrupt) {
    sm::log::Writef(sm::LogLevel::Warn, "host", "界面状态文件已损坏，本次用默认值（%s）",
                    error.c_str());
  }

  return ExitCode::kOk;
}

bool App::AcquireSingleInstance() {
  instance_lock_ = platform::acquire_single_instance_lock(paths_.config_dir.c_str());
  owns_instance_ = instance_lock_ != nullptr;
  if (owns_instance_) return true;

  if (!config_.App().single_instance) {
    sm::log::Write(sm::LogLevel::Warn, "host",
                   "single_instance = false：允许多开，配置写回可能互相覆盖");
    return true;
  }

  if (!platform::notify_existing_instance(paths_.config_dir.c_str())) {
    sm::log::Write(sm::LogLevel::Warn, "host", "未能唤起已有实例（它可能正在启动中）");
  }
  sm::log::Write(sm::LogLevel::Warn, "host", "已有实例在运行，本次启动退出");
  return false;
}

bool App::InitPlatform() {
  renderer_ = ProbeRenderer(renderer_);

  if (!platform::window::Init(ClientApiFor(renderer_))) return false;

  LogAttachedMonitors();

  if (!messages_.Create(&App::MessageWindowThunk, this)) {
    return false;
  }

  const std::string title = PanelTitle();
  platform::window::WindowOptions options;
  options.title = title;
  options.width = kPanelDefaultWidth;
  options.height = kPanelDefaultHeight;
  options.min_width = kPanelMinWidth;
  options.min_height = kPanelMinHeight;
  options.visible = false;
  options.callbacks.on_close_request = &App::WindowCloseRequestThunk;
  options.callbacks.on_content_scale = &App::WindowContentScaleThunk;
  options.callbacks.user = this;

  window_.reset(platform::window::NewWindow());
  if (window_ == nullptr || !window_->Create(options)) {
    return false;
  }
  applied_title_ = title;

  renderer_ = ResolveAgainstWindow(renderer_, window_->client_api());

  window_->SetRect(ui_state_.x, ui_state_.y, ui_state_.w, ui_state_.h);

  tray_ = platform::win32::create_tray();
  if (tray_ == nullptr || !tray_->Create(MakeTrayLabels(), this)) {
    delete tray_;
    tray_ = nullptr;
    sm::log::Write(sm::LogLevel::Warn, "host", strings::kNoTrayHint);
  }
  return true;
}

bool App::WireSubsystems() {

  internal_source_.SetSink(&pipeline_);
  internal_source_.SetNotifyLogLevel(config_.App().notify_log_level);

  scheduler_.SetPluginRunner(&plugins_);
  scheduler_.SetNotificationSink(&pipeline_);

  pipeline_.SetRouter(&router_);
  pipeline_.SetDelivery(&plugins_);
  pipeline_.SetHistoryCapacity(
      static_cast<std::size_t>(config_.App().notification_history_size));

  actions_.SetPipeline(&pipeline_);
  actions_.SetRouter(&router_);
  actions_.SetRunner(&plugins_);

  plugins_.SetScheduleSink(&scheduler_);
  plugins_.SetUiSession(&session_);
  icons_.SetDataDir(paths_.data_dir);
  plugins_.SetIconProvider(&icons_);
  plugins_.SetActionSink(this);
  plugins_.SetEventSink(this);

  session_.SetFontSize(config_.App().ui_font_size);
  session_.SetRuntime(runtime_ =
                          CreateImGuiRuntime(window_.get(), renderer_,
                                             WindowSizing::kUserDriven, &frame_));
  session_.SetNotifyRuntimeSource(&App::NotifyRuntimeFactoryThunk, this);

  UiPanelSetup setup;
  setup.host = this;
  setup.config = &config_;
  setup.plugins = &plugins_;
  setup.pipeline = &pipeline_;
  setup.router = &router_;
  setup.scheduler = &scheduler_;
  setup.session = &session_;
  setup.actions = &actions_;

  panel_.reset(new (std::nothrow) UiPanel(setup, &ui_state_));
  return panel_ != nullptr;
}

void App::RebuildRouting() {
  router_.Rebuild(
      BuildSubscribers(plugins_.ActiveNotifierSubscriptions(), config_.Data().routing));
}

void App::RunMessageLoop() {
  while (!exiting_) {
    platform::window::Pump();

    const std::int64_t now = platform::monotonic_ms();
    FrameStep(now);
    if (exiting_) break;

    platform::window::Wait(static_cast<int>(NextWaitMs(now)));
  }
}

void App::FrameStep(std::int64_t now_ms) {
  icons_.BeginFrame(now_ms);
  plugins_.PumpMainThread();
  const std::size_t delivered = pipeline_.PumpMainThread();
  actions_.PumpMainThread();

  if (delivered > 0 && notify_empty_since_ms_ != 0) {
    if (NotifyDormant(now_ms)) {
      sm::log::Write(sm::LogLevel::Info, "ui", "有新通知到达，通知表面从休眠中醒来");
    }
    ClearNotifyDormancy();
  }

  const UiSession::Changes change = session_.Tick(now_ms);
  if (change.panel == UiSession::Change::Created) {
    window_->Show();
    ReportFontFallback(runtime_);
  } else if (change.panel == UiSession::Change::Destroyed) {
    window_->Hide();
    SaveUiStateNow();
  }

  if (change.notify == UiSession::Change::Created) {
    ClearNotifyDormancy();
    ReportFontFallback(notify_runtime_);
  } else if (change.notify == UiSession::Change::Destroyed) {
    if (notify_window_ != nullptr) notify_window_->Hide();
    ClearNotifyDormancy();
  }

  SyncDetailSurfaces();

  UpdateWindowTitle();
  UpdateTrayLevel();

  // 休眠中的浮层不算"在画"：否则主循环会被按在浮层的 vsync 上白烧 CPU。
  const bool notify_dormant = NotifyDormant(now_ms);
  if (notify_dormant && !notify_dormant_logged_) {
    notify_dormant_logged_ = true;
    sm::log::Writef(sm::LogLevel::Info, "ui",
                    "通知表面没有内容已 %lld 秒，进入休眠（不再渲染）",
                    static_cast<long long>(kNotifyDormantMs / 1000));
  }
  if (session_.notify_runtime_alive() && notify_frame_ != nullptr &&
      !notify_dormant) {
    const platform::window::WorkArea area =
        NotifyWorkArea(config_.Data().notify_window.monitor);
    notify_runtime_->SetViewportSize(area.width, area.height);

    notify_frame_->RenderFrame(&App::DrawNotifyThunk, this);
    const ContentBounds content = notify_frame_->content_bounds();
    PlaceNotifyWindow(content);
    if (!content.empty) {
      ClearNotifyDormancy();
    } else if (notify_empty_since_ms_ == 0) {
      notify_empty_since_ms_ = now_ms;
    }
  }
  if (session_.panel_alive() && session_.panel_runtime_alive() && frame_ != nullptr) {
    frame_->RenderFrame(&App::DrawPanelThunk, this);
  }

  for (std::size_t i = 0; i < details_.size();) {
    DetailSurface* const surface = details_[i].get();
    if (surface->frame == nullptr ||
        surface->frame->RenderFrame(&App::DrawDetailThunk, surface)) {
      ++i;
      continue;
    }

    sm::log::Writef(sm::LogLevel::Error, "ui", "详情窗口渲染失败（插件 %s）",
                    surface->plugin_id.c_str());
    session_.CloseView(surface->plugin_id);
    detail_failed_.push_back(surface->plugin_id);
    DestroyDetailSurface(surface);
    details_.erase(details_.begin() + static_cast<std::ptrdiff_t>(i));
  }

  config_.Tick(now_ms);
  DrainConfigDiagnostics();
  internal_source_.Tick(now_ms);
  scheduler_.PollWatchdog();
}

bool App::NotifyDormant(std::int64_t now_ms) const {
  return notify_empty_since_ms_ != 0 &&
         now_ms - notify_empty_since_ms_ >= kNotifyDormantMs;
}

void App::ClearNotifyDormancy() {
  notify_empty_since_ms_ = 0;
  notify_dormant_logged_ = false;
}

std::int64_t App::NextWaitMs(std::int64_t now_ms) const {
  if ((session_.panel_alive() && session_.panel_runtime_alive()) ||
      (session_.notify_alive() && session_.notify_runtime_alive() &&
       !NotifyDormant(now_ms)) ||
      !details_.empty()) {
    return 0;
  }
  if (pipeline_.queued() > 0 || actions_.queued() > 0 ||
      plugins_.main_thread_queue_size() > 0) {
    return 0;
  }

  std::int64_t wait = kIdleWakeMs;
  const std::int64_t due = config_.next_write_due_ms();
  if (due > 0) {
    const std::int64_t remain = due - now_ms;
    if (remain < wait) wait = (remain > 1) ? remain : 1;
  }
  return wait;
}

void App::DrawNotifyContent() {
  DrawNotifierSurfaces();
}

void App::DrawNotifyThunk(void* user) {
  auto* const self = static_cast<App*>(user);
  self->session_.SetDrawingSurface(UiSession::Surface::kNotify);
  self->icons_.SetCurrentRuntime(self->notify_runtime_);
  self->DrawNotifyContent();
}

void App::DrawPanelContent() {
  if (panel_ == nullptr) return;

  RootWindow root(kPanelRootId);
  if (!root.visible()) return;
  panel_->Draw();
}

void App::DrawPanelThunk(void* user) {
  auto* const self = static_cast<App*>(user);
  self->session_.SetDrawingSurface(UiSession::Surface::kPanel);
  self->icons_.SetCurrentRuntime(self->runtime_);
  self->DrawPanelContent();
}

void App::ReportFontFallback(const IUiRuntime* runtime) {
  if (runtime == nullptr || font_fallback_reported_) return;
  if (!runtime->font_file().empty()) return;
  font_fallback_reported_ = true;
  internal_source_.PostEvent(HostEvent::UiFontFallback, "", strings::kFontFallbackBuiltin);
}

void App::DrawNotifierSurfaces() {
  const std::vector<const OwnedNotification*> history = pipeline_.History();

  std::vector<sm::Notification> visible;
  for (const Subscriber& subscriber : router_.subscribers()) {
    visible.clear();
    for (const OwnedNotification* n : history) {
      if (SubscriberAccepts(subscriber, n->topic)) visible.push_back(n->view);
    }
    plugins_.InvokeDrawUi(
        subscriber.id, sm::Span<const sm::Notification>(visible.data(), visible.size()));
  }
}

App::DetailSurface* App::FindDetailSurface(const std::string& plugin_id) {
  for (const std::unique_ptr<DetailSurface>& surface : details_) {
    if (surface->plugin_id == plugin_id) return surface.get();
  }
  return nullptr;
}

bool App::CreateDetailSurface(const UiSession::ViewRef& view) {
  std::unique_ptr<DetailSurface> surface(new (std::nothrow) DetailSurface());
  if (surface == nullptr) return false;

  surface->app = this;
  surface->plugin_id = view.plugin_id;
  surface->title = view.title;

  platform::window::WindowOptions options;
  options.title = view.title;
  options.width = kDetailDefaultWidth;
  options.height = kDetailDefaultHeight;
  options.min_width = kDetailMinWidth;
  options.min_height = kDetailMinHeight;
  options.visible = false;
  options.callbacks.on_close_request = &App::DetailCloseRequestThunk;
  options.callbacks.on_content_scale = &App::DetailContentScaleThunk;
  options.callbacks.user = surface.get();

  surface->window.reset(platform::window::NewWindow());
  if (surface->window == nullptr || !surface->window->Create(options)) {
    surface->window.reset();
    sm::log::Writef(sm::LogLevel::Warn, "ui", "详情窗口创建失败（插件 %s）",
                    view.plugin_id.c_str());
    return false;
  }

  const platform::window::WorkArea area = platform::window::PrimaryWorkArea();
  const int offset =
      static_cast<int>(details_.size()) * kDetailCascadeOffset;
  const int x = area.x + (area.width - kDetailDefaultWidth) / 2 + offset;
  const int y = area.y + (area.height - kDetailDefaultHeight) / 2 + offset;
  surface->window->SetRect(x, y, kDetailDefaultWidth, kDetailDefaultHeight);

  surface->runtime = CreateImGuiRuntime(surface->window.get(), renderer_,
                                        WindowSizing::kUserDriven, &surface->frame);
  if (surface->runtime == nullptr ||
      !surface->runtime->Create(&surface->context, session_.font_size())) {
    if (surface->runtime != nullptr) surface->runtime->Destroy();
    delete surface->runtime;
    surface->runtime = nullptr;
    surface->frame = nullptr;
    surface->context = nullptr;
    surface->window.reset();
    return false;
  }
  surface->font_size_px = session_.font_size();

  surface->window->Show();
  sm::log::Writef(sm::LogLevel::Info, "ui", "详情窗口已打开（插件 %s，标题 %s）",
                  view.plugin_id.c_str(), view.title.c_str());
  details_.push_back(std::move(surface));
  return true;
}

void App::DestroyDetailSurface(DetailSurface* surface) {
  if (surface == nullptr) return;
  if (surface->runtime != nullptr) {
    surface->runtime->Destroy();
    delete surface->runtime;
    surface->runtime = nullptr;
    surface->frame = nullptr;
    surface->context = nullptr;
  }
  surface->window.reset();
}

void App::DestroyAllDetailSurfaces() {
  for (const std::unique_ptr<DetailSurface>& surface : details_) {
    DestroyDetailSurface(surface.get());
  }
  details_.clear();
  detail_failed_.clear();
}

void App::SyncDetailSurfaces() {
  const std::vector<UiSession::ViewRef> views = session_.Views();
  const auto has_view = [&views](const std::string& plugin_id) {
    for (const UiSession::ViewRef& view : views) {
      if (view.plugin_id == plugin_id) return true;
    }
    return false;
  };

  for (std::size_t i = details_.size(); i > 0; --i) {
    if (has_view(details_[i - 1]->plugin_id)) continue;
    DestroyDetailSurface(details_[i - 1].get());
    details_.erase(details_.begin() + static_cast<std::ptrdiff_t>(i - 1));
  }
  for (std::size_t i = detail_failed_.size(); i > 0; --i) {
    if (!has_view(detail_failed_[i - 1])) {
      detail_failed_.erase(detail_failed_.begin() +
                           static_cast<std::ptrdiff_t>(i - 1));
    }
  }

  for (const UiSession::ViewRef& view : views) {
    DetailSurface* const surface = FindDetailSurface(view.plugin_id);
    if (surface != nullptr) {
      if (surface->title != view.title) {
        surface->title = view.title;
        surface->window->SetTitle(view.title);
      }
      if (surface->font_size_px != session_.font_size()) {
        surface->runtime->RebuildFonts(session_.font_size());
        surface->font_size_px = session_.font_size();
      }
      continue;
    }

    bool failed = false;
    for (const std::string& id : detail_failed_) {
      if (id == view.plugin_id) {
        failed = true;
        break;
      }
    }
    if (failed) continue;

    if (!CreateDetailSurface(view)) detail_failed_.push_back(view.plugin_id);
  }
}

void App::DrawDetailContent(DetailSurface* surface) {
  if (surface == nullptr) return;

  // 与通知表面同理：每帧都要把"这块内容画在哪个上下文"告诉会话，插件
  // draw_view() 里取 imgui_context() 时才会拿到本窗口自己的那份。
  session_.SetDrawingSurface(UiSession::Surface::kDetail);
  session_.SetDetailContext(surface->context);
  icons_.SetCurrentRuntime(surface->runtime);

  RootWindow root(kDetailRootId);
  if (!root.visible()) return;
  plugins_.InvokeDrawView(surface->plugin_id);
}

void App::DrawDetailThunk(void* user) {
  auto* const surface = static_cast<DetailSurface*>(user);
  if (surface != nullptr && surface->app != nullptr) {
    surface->app->DrawDetailContent(surface);
  }
}

void App::DetailCloseRequestThunk(void* user) {
  auto* const surface = static_cast<DetailSurface*>(user);
  if (surface == nullptr || surface->app == nullptr) return;
  sm::log::Writef(sm::LogLevel::Info, "ui", "详情窗口收到关闭请求（插件 %s）",
                  surface->plugin_id.c_str());
  surface->app->session_.CloseView(surface->plugin_id);
}

void App::DetailContentScaleThunk(void* user, float scale) {
  auto* const surface = static_cast<DetailSurface*>(user);
  if (surface != nullptr && surface->runtime != nullptr) {
    surface->runtime->SetContentScale(scale);
  }
}

void App::HandleWindowCloseRequest() {
  if (!window_->visible()) {
    sm::log::Write(sm::LogLevel::Warn, "host", "消息队列收到 WM_QUIT，正在退出");
    RequestExit();
    return;
  }

  RequestPanelClose();
}

void App::WindowCloseRequestThunk(void* user) {
  static_cast<App*>(user)->HandleWindowCloseRequest();
}

void App::OnContentScale(float scale) {
  session_.SetContentScale(UiSession::Surface::kPanel, scale);
}

void App::WindowContentScaleThunk(void* user, float scale) {
  static_cast<App*>(user)->OnContentScale(scale);
}

void App::OnNotifyContentScale(float scale) {
  session_.SetContentScale(UiSession::Surface::kNotify, scale);
  notify_area_valid_ = false;
}

void App::NotifyContentScaleThunk(void* user, float scale) {
  static_cast<App*>(user)->OnNotifyContentScale(scale);
}

platform::window::WorkArea App::NotifyWorkArea(const std::string& monitor) {
  if (!notify_area_valid_ || notify_area_name_ != monitor) {
    notify_area_ = ResolveWorkArea(monitor);
    notify_area_name_ = monitor;
    notify_area_valid_ = true;
  }
  return notify_area_;
}

void App::NotifyPlacement(const config::NotifyWindowConfig& want, int w, int h,
                          int* out_x, int* out_y) {
  const platform::window::WorkArea area = NotifyWorkArea(want.monitor);
  const int left = area.x + kNotifyMargin;
  const int top = area.y + kNotifyMargin;
  const int right = area.x + area.width - w - kNotifyMargin;
  const int bottom = area.y + area.height - h - kNotifyMargin;

  switch (want.corner) {
    case config::NotifyCorner::kTopLeft:     *out_x = left;  *out_y = top;    break;
    case config::NotifyCorner::kTopRight:    *out_x = right; *out_y = top;    break;
    case config::NotifyCorner::kBottomLeft:  *out_x = left;  *out_y = bottom; break;
    case config::NotifyCorner::kBottomRight: *out_x = right; *out_y = bottom; break;
  }
}

void App::PlaceNotifyWindow(const ContentBounds& content) {
  if (notify_window_ == nullptr || !notify_window_->alive()) return;

  // 没有内容必须把整扇窗口藏掉：一扇不透明的空窗点得到、看不见，还会挡住底下的窗口。
  if (content.empty) {
    if (notify_window_->visible()) notify_window_->Hide();
    return;
  }

  const config::NotifyWindowConfig& want = config_.Data().notify_window;
  int x = 0;
  int y = 0;
  NotifyPlacement(want, content.w, content.h, &x, &y);

  // 尺寸与位置必须同一帧一起定下来（否则卡片逐帧跳），且只在真变了时才 SetRect。
  int cur_x = 0, cur_y = 0, cur_w = 0, cur_h = 0;
  notify_window_->GetRect(&cur_x, &cur_y, &cur_w, &cur_h);
  if (cur_x != x || cur_y != y || cur_w != content.w || cur_h != content.h) {
    notify_window_->SetRect(x, y, content.w, content.h);
  }
  if (!notify_window_->visible()) notify_window_->Show(false);
}

IUiRuntime* App::CreateNotifyRuntime() {
  if (notify_runtime_ != nullptr) return notify_runtime_;

  notify_area_valid_ = false;

  platform::window::WindowOptions options;
  options.title = strings::kNotifyWindowTitle;
  options.width = kNotifyDefaultWidth;
  options.height = kNotifyDefaultHeight;
  options.visible = false;  // 刚建出来的那帧内容尺寸还没量过，先显示会闪一下错误的大小
  options.traits.decorated = false;
  options.traits.resizable = false;
  options.traits.floating = true;
  options.traits.no_activate = true;
  options.traits.tool_window = true;
  options.callbacks.on_close_request = &App::NotifyCloseRequestThunk;
  options.callbacks.on_content_scale = &App::NotifyContentScaleThunk;
  options.callbacks.user = this;

  notify_window_.reset(platform::window::NewWindow());
  if (notify_window_ == nullptr || !notify_window_->Create(options)) {
    notify_window_.reset();
    return nullptr;
  }

  const config::NotifyWindowConfig& want = config_.Data().notify_window;
  int x = 0;
  int y = 0;
  NotifyPlacement(want, kNotifyDefaultWidth, kNotifyDefaultHeight, &x, &y);
  notify_window_->SetRect(x, y, kNotifyDefaultWidth, kNotifyDefaultHeight);

  notify_runtime_ = CreateImGuiRuntime(notify_window_.get(), renderer_,
                                       WindowSizing::kContentDriven, &notify_frame_);
  if (notify_runtime_ == nullptr) {
    notify_frame_ = nullptr;
    notify_window_.reset();
  }
  return notify_runtime_;
}

IUiRuntime* App::NotifyRuntimeFactoryThunk(void* user) {
  return static_cast<App*>(user)->CreateNotifyRuntime();
}

void App::HandleNotifyCloseRequest() {
  sm::log::Write(sm::LogLevel::Info, "ui",
                 "通知表面收到关闭请求（该不该消失由引用计数决定）");
}

void App::NotifyCloseRequestThunk(void* user) {
  static_cast<App*>(user)->HandleNotifyCloseRequest();
}

bool App::HandleMessageWindowMessage(HWND , UINT message, WPARAM wparam,
                                     LPARAM ) {
  if (message == platform::win32::kWakeMessage) {
    OpenPanel();
    return true;
  }

  if (message == WM_QUERYENDSESSION) {
    sm::log::Write(sm::LogLevel::Info, "host", "系统正在关机或注销，准备退出");
    return false;
  }

  if (message == WM_ENDSESSION) {
    if (wparam != 0) RequestExit();
    return false;
  }

  return false;
}

bool App::MessageWindowThunk(void* user, HWND hwnd, UINT message, WPARAM wparam,
                             LPARAM lparam) {
  return static_cast<App*>(user)->HandleMessageWindowMessage(hwnd, message, wparam,
                                                             lparam);
}

void App::Shutdown() {
  const std::int64_t now = platform::monotonic_ms();

  session_.SetPanelOpen(false);
  for (const PluginInfo& info : plugins_.Snapshot()) session_.ReleasePlugin(info.id);
  session_.Tick(now);
  DestroyAllDetailSurfaces();
  SaveUiStateNow();
  session_.SetRuntime(nullptr);
  delete runtime_;
  runtime_ = nullptr;
  frame_ = nullptr;
  session_.SetNotifyRuntimeSource(nullptr, nullptr);
  delete notify_runtime_;
  notify_runtime_ = nullptr;
  notify_frame_ = nullptr;

  plugins_.BeginShutdown();
  scheduler_.request_stop();
  scheduler_.join(kShutdownGraceMs);

  plugins_.ShutdownAll();

  if (const std::size_t left = pipeline_.queued(); left > 0) {
    sm::log::Writef(sm::LogLevel::Warn, "host", "退出时丢弃了 %zu 条未消费的通知", left);
  }
  if (const std::size_t left = actions_.queued(); left > 0) {
    sm::log::Writef(sm::LogLevel::Warn, "host", "退出时丢弃了 %zu 个未分发的动作点击", left);
  }
  config_.FlushNow();
  DrainConfigDiagnostics();

  internal_source_.PostEvent(HostEvent::HostExited, "", strings::kHostExitedDetail);
  pipeline_.PumpMainThread();

  if (tray_ != nullptr) {
    tray_->Destroy();
    delete tray_;
    tray_ = nullptr;
  }
  notify_window_.reset();
  window_.reset();
  messages_.Destroy();
  platform::window::Shutdown();

  sm::log::Write(sm::LogLevel::Info, "host", "SimpleMessage 已退出");
}

void App::DrainConfigDiagnostics() {
  config::Diagnostics diagnostics;
  config_.DrainDiagnostics(&diagnostics);
  if (diagnostics.empty()) return;

  LogConfigDiagnostics(diagnostics);

  const config::Diagnostic* last_error = nullptr;
  for (const config::Diagnostic& d : diagnostics) {
    if (d.severity == config::Diagnostic::Severity::Error) last_error = &d;
  }
  if (last_error != nullptr) {
    internal_source_.PostEvent(HostEvent::ConfigWriteFailed, "", last_error->message);
  }
}

void App::OpenPanel() {
  if (panel_ != nullptr) panel_->Open();
}

void App::RequestPanelClose() {
  if (panel_ != nullptr) panel_->RequestClose();
}

void App::SaveUiStateNow() {
  if (!UiStateWritable(ui_state_load_) || config_.read_only()) return;

  if (panel_ != nullptr) panel_->CaptureWindowRect();
  std::string error;
  if (!SaveUiState(UiStatePath(paths_), ui_state_, &error)) {
    sm::log::Writef(sm::LogLevel::Warn, "host", "界面状态未能保存（%s）", error.c_str());
  }
}

void App::UpdateWindowTitle() {
  const std::string title = PanelTitle();
  if (title == applied_title_) return;
  applied_title_ = title;
  window_->SetTitle(title);
}

std::string App::PanelTitle() const {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), strings::kWindowTitleFormat, SM_VERSION_STRING);
  return std::string(buffer);
}

void App::UpdateTrayLevel() {
  if (tray_ == nullptr) return;

  const bool panel_open = session_.panel_open();

  if (panel_open) pipeline_.MarkAllRead();
  const std::vector<const OwnedNotification*> history = pipeline_.History();

  int rank = -1;
  for (const OwnedNotification* n : history) {
    if (n->read) continue;
    if (static_cast<int>(n->level) > rank) rank = static_cast<int>(n->level);
  }

  if (rank == tray_shown_rank_) return;
  tray_shown_rank_ = rank;
  if (rank < 0) {
    tray_->SetLevel(nullptr);
    return;
  }
  const sm::Level level = static_cast<sm::Level>(rank);
  tray_->SetLevel(&level);
}

bool App::ShellOpen(const std::string& utf8_target) const {
  if (utf8_target.empty()) return false;

  const std::wstring target = util::widen(utf8_target.c_str());
  const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", target.c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<std::intptr_t>(result) > 32;
}

const char* App::log_file_path() const {
  log_path_cache_.resize(1024);
  const std::size_t length =
      sm::log::CurrentFilePath(&log_path_cache_[0], log_path_cache_.size());
  if (length == 0) return "";
  log_path_cache_.resize(length);
  return log_path_cache_.c_str();
}

const char* App::font_file_path() const {
  return (runtime_ != nullptr) ? runtime_->font_file().c_str() : "";
}

bool App::config_unsaved() const { return config_.last_write_failed(); }

void App::RetryConfigSave() {
  config_.FlushNow();
}

void App::OpenInShell(const std::string& utf8_path) {
  if (!ShellOpen(utf8_path)) {
    sm::log::Writef(sm::LogLevel::Warn, "host", "无法打开 %s", utf8_path.c_str());
  }
}

void App::RequestExit() { exiting_ = true; }

void App::NotifyCloseHint() {
  if (tray_ != nullptr) tray_->ShowBalloon(strings::kCloseHintTitle, strings::kCloseHintText);
}

void App::CurrentWindowRect(int* x, int* y, int* w, int* h) const {
  window_->GetRect(x, y, w, h);
}

bool App::InvokeAction(const std::string& plugin_id, std::int64_t notification_seq,
                       const char* action_id) {
  (void)plugin_id;
  return actions_.PostAction(notification_seq, action_id);
}

bool App::OpenUrl(const char* url) {
  if (url == nullptr) return false;

  const bool allowed = HasPrefixCI(url, "http://") || HasPrefixCI(url, "https://") ||
                       HasPrefixCI(url, "mailto:");
  if (!allowed) {
    sm::log::Writef(sm::LogLevel::Warn, "host",
                    "插件请求打开的链接协议不受支持（已拒绝）：%s", url);
    return false;
  }
  return ShellOpen(url);
}

void App::OnPluginStateChanged(const std::string& plugin_id, PluginState state,
                              const char* reason, bool crashed) {
  internal_source_.OnPluginStateChanged(plugin_id, state, reason, crashed);

  if (state == PluginState::Unloaded) session_.ReleasePlugin(plugin_id);

  RebuildRouting();
}

bool App::pause_checked() const { return scheduler_.paused(); }

void App::OnOpenPanel() { OpenPanel(); }

void App::OnTogglePause() {
  const bool paused = !scheduler_.paused();
  scheduler_.set_paused(paused);
  sm::log::Write(sm::LogLevel::Info, "host", paused ? "调度已暂停" : "调度已恢复");
}

void App::OnOpenConfigDir() { OpenInShell(paths_.config_dir); }

void App::OnOpenLogDir() { OpenInShell(paths_.log_dir); }

void App::OnAbout() {
  if (panel_ != nullptr) panel_->OpenAt(PanelPage::About);
}

void App::OnExit() { RequestExit(); }

}  // namespace host
}  // namespace sm

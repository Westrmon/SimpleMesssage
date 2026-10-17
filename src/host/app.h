// SimpleMessage — 应用外壳：启动顺序、消息循环、关停顺序
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "host/action_dispatch.h"
#include "host/config.h"
#include "host/image_cache.h"
#include "host/internal_source.h"
#include "host/message_window.h"
#include "host/notification_pipeline.h"
#include "host/plugin_manager.h"
#include "host/render_backend.h"
#include "host/router.h"
#include "host/scheduler.h"
#include "host/ui_panel.h"
#include "host/ui_session.h"
#include "host/ui_state.h"

#include "platform/window.h"
#include "platform/win32/tray.h"

namespace sm {
namespace host {

enum class ExitCode : int {
  kOk = 0,
  kFatal = 1,
  kConfigTooNew = 2,
  kPlatform = 3,
};

class App final : public IPanelHost,
                  public IPluginEventSink,
                  public IPluginActionSink,
                  public platform::win32::ITrayHost {
 public:
  App();
  ~App() override;

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int Run(int argc, wchar_t** argv);

 private:
  enum class ArgsResult { kContinue, kHandled };

  ArgsResult ParseArgs(int argc, wchar_t** argv);
  ExitCode StartupConfigAndLogging();
  bool AcquireSingleInstance();
  bool InitPlatform();
  bool WireSubsystems();
  void RebuildRouting();

  void RunMessageLoop();
  void FrameStep(std::int64_t now_ms);
  std::int64_t NextWaitMs(std::int64_t now_ms) const;
  void DrawNotifyContent();
  static void DrawNotifyThunk(void* user);
  void DrawPanelContent();
  static void DrawPanelThunk(void* user);
  void DrawNotifierSurfaces();

  // 详情视图：每个插件一扇独立窗口，只画该插件的 draw_view()。
  struct DetailSurface {
    App*        app = nullptr;
    std::string plugin_id;
    std::string title;
    std::unique_ptr<platform::window::IWindow> window;
    IUiRuntime*       runtime = nullptr;
    IUiFrameRenderer* frame = nullptr;
    void*             context = nullptr;
    int               font_size_px = 0;
  };
  void SyncDetailSurfaces();
  bool CreateDetailSurface(const UiSession::ViewRef& view);
  void DestroyDetailSurface(DetailSurface* surface);
  void DestroyAllDetailSurfaces();
  DetailSurface* FindDetailSurface(const std::string& plugin_id);
  void DrawDetailContent(DetailSurface* surface);
  static void DrawDetailThunk(void* user);
  static void DetailCloseRequestThunk(void* user);
  static void DetailContentScaleThunk(void* user, float scale);

  IUiRuntime* CreateNotifyRuntime();
  static IUiRuntime* NotifyRuntimeFactoryThunk(void* user);
  void NotifyPlacement(const config::NotifyWindowConfig& want, int w, int h,
                       int* out_x, int* out_y);
  platform::window::WorkArea NotifyWorkArea(const std::string& monitor);
  void PlaceNotifyWindow(const ContentBounds& content);
  bool NotifyDormant(std::int64_t now_ms) const;
  void ClearNotifyDormancy();
  void HandleNotifyCloseRequest();
  static void NotifyCloseRequestThunk(void* user);
  void OnNotifyContentScale(float scale);
  static void NotifyContentScaleThunk(void* user, float scale);

  void HandleWindowCloseRequest();
  static void WindowCloseRequestThunk(void* user);
  void OnContentScale(float scale);
  static void WindowContentScaleThunk(void* user, float scale);
  void ReportFontFallback(const IUiRuntime* runtime);
  bool HandleMessageWindowMessage(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam);
  static bool MessageWindowThunk(void* user, HWND hwnd, UINT message, WPARAM wparam,
                                 LPARAM lparam);

  void Shutdown();
  void DrainConfigDiagnostics();

  void OpenPanel();
  void RequestPanelClose();
  void SaveUiStateNow();
  void UpdateWindowTitle();
  void UpdateTrayLevel();
  std::string PanelTitle() const;
  bool ShellOpen(const std::string& utf8_target) const;

  const config::Paths& paths() const override { return paths_; }
  const char*          log_file_path() const override;
  const char*          font_file_path() const override;
  bool                 safe_mode() const override { return safe_mode_; }
  bool                 owns_single_instance() const override { return owns_instance_; }
  std::int64_t         started_monotonic_ms() const override { return started_ms_; }
  bool                 config_unsaved() const override;
  void                 RetryConfigSave() override;
  void                 OpenInShell(const std::string& utf8_path) override;
  void                 RequestExit() override;
  void                 NotifyCloseHint() override;
  void                 CurrentWindowRect(int* x, int* y, int* w, int* h) const override;

  bool InvokeAction(const std::string& plugin_id, std::int64_t notification_seq,
                    const char* action_id) override;
  bool OpenUrl(const char* url) override;

  void OnPluginStateChanged(const std::string& plugin_id, PluginState state,
                            const char* reason, bool crashed) override;

  bool pause_checked() const override;
  void OnOpenPanel() override;
  void OnTogglePause() override;
  void OnOpenConfigDir() override;
  void OnOpenLogDir() override;
  void OnAbout() override;
  void OnExit() override;

  std::string           config_dir_arg_;
  std::string           data_dir_arg_;
  std::string           plugin_dir_arg_;
  config::PathOverrides overrides_{};
  bool                  console_ = false;
  bool                  safe_mode_ = false;

  RendererKind          renderer_ = RendererKind::kAuto;
  config::Paths         paths_{};

  config::ConfigStore config_;

  Router               router_;
  NotificationPipeline pipeline_;
  ActionDispatcher     actions_;
  InternalSource       internal_source_;

  PluginManager plugins_;
  Scheduler     scheduler_;

  MessageWindow     messages_;
  std::unique_ptr<platform::window::IWindow> window_;
  IUiRuntime*       runtime_ = nullptr;
  IUiFrameRenderer* frame_ = nullptr;
  std::unique_ptr<platform::window::IWindow> notify_window_;
  IUiRuntime*       notify_runtime_ = nullptr;
  IUiFrameRenderer* notify_frame_ = nullptr;
  platform::window::WorkArea notify_area_{};
  std::string               notify_area_name_;
  bool                      notify_area_valid_ = false;
  std::int64_t notify_empty_since_ms_ = 0;
  bool         notify_dormant_logged_ = false;
  std::vector<std::unique_ptr<DetailSurface>> details_;
  std::vector<std::string>                    detail_failed_;
  ImageCache        icons_;
  UiSession         session_;
  UiState           ui_state_{};
  UiStateLoad       ui_state_load_ = UiStateLoad::Missing;
  std::unique_ptr<UiPanel> panel_;

  platform::win32::ITray*        tray_ = nullptr;
  platform::ISingleInstanceLock* instance_lock_ = nullptr;
  bool                           owns_instance_ = false;

  std::int64_t started_ms_ = 0;
  bool         exiting_ = false;
  std::string  applied_title_;
  int          tray_shown_rank_ = -1;
  bool         font_fallback_reported_ = false;
  mutable std::string log_path_cache_;
};

}  // namespace host
}  // namespace sm

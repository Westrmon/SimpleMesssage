// SimpleMessage — 控制面板：六页 + 插件动态视图页
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "host/action_dispatch.h"
#include "host/config.h"
#include "host/log.h"
#include "host/monitor.h"
#include "host/notification_pipeline.h"
#include "host/plugin_manager.h"
#include "host/router.h"
#include "host/scheduler.h"
#include "host/settings_form.h"
#include "host/ui_session.h"
#include "host/ui_state.h"

namespace sm {
namespace host {

void LogConfigDiagnostics(const config::Diagnostics& diagnostics);

class IPanelHost {
 public:
  virtual ~IPanelHost() = default;

  virtual const config::Paths& paths() const = 0;
  virtual const char* log_file_path() const = 0;
  virtual const char* font_file_path() const = 0;
  virtual bool        safe_mode() const = 0;
  virtual bool        owns_single_instance() const = 0;
  virtual std::int64_t started_monotonic_ms() const = 0;
  virtual bool config_unsaved() const = 0;
  virtual void RetryConfigSave() = 0;
  virtual void OpenInShell(const std::string& utf8_path) = 0;
  virtual void RequestExit() = 0;
  virtual void NotifyCloseHint() = 0;
  virtual void CurrentWindowRect(int* x, int* y, int* w, int* h) const = 0;
};

struct UiPanelSetup {
  IPanelHost*                 host = nullptr;
  config::ConfigStore*        config = nullptr;
  PluginManager*              plugins = nullptr;
  NotificationPipeline*       pipeline = nullptr;
  const Router*               router = nullptr;
  const Scheduler*            scheduler = nullptr;
  UiSession*                  session = nullptr;
  IActionDispatcher*          actions = nullptr;
};

class UiPanel {
 public:
  UiPanel(UiPanelSetup setup, UiState* state);
  ~UiPanel();
  UiPanel(const UiPanel&) = delete;
  UiPanel& operator=(const UiPanel&) = delete;

  void Open();
  void OpenAt(PanelPage page);
  bool RequestClose();
  void OpenForViews();
  bool open() const;

  void Draw();

  bool modal_active() const;

  void CaptureWindowRect();
  PanelPage page() const;

 private:

  void DrawFixedPage(PanelPage target);
  void DrawOverviewPage();
  void DrawPluginsPage();
  void DrawNotificationsPage();
  void DrawRoutingPage();
  void DrawLogPage();
  void DrawAboutPage();
  void DrawPluginDetail(const PluginInfo& info);
  void DrawViewPages();
  void SyncKnownViews(const std::vector<UiSession::ViewRef>& views);

  void DrawBanners();
  void DrawTabBar();
  void DrawStillInUseNotice();
  void DrawConfirmPopups();

  void GoToPluginPage(const std::string& plugin_id);
  void OnLogSnapshot();
  void ExportNotifications(const std::vector<const OwnedNotification*>& items);
  std::string ui_state_path() const;

  UiPanelSetup setup_;
  UiState*     state_ = nullptr;

  std::int64_t selected_seq_ = 0;
  std::string        expanded_plugin_;
  std::string        scroll_to_plugin_;
  bool               pending_tab_select_ = false;
  std::string        select_view_;
  std::vector<std::string> known_views_;
  TextInputState     form_state_;
  TextInputState     notify_topic_state_;
  TextInputState     notify_text_state_;
  TextInputState     log_text_state_;
  static constexpr std::size_t kLogSnapshotMax = 256;
  std::vector<log::Entry> log_entries_;
  std::uint64_t           log_version_ = 0;
  std::uint64_t           log_dropped_ = 0;
  std::vector<std::string> notify_sources_;
  std::vector<std::string> log_sources_;
  std::string export_message_;
  bool        export_failed_ = false;
  bool        clear_history_pending_ = false;
  bool        reset_settings_pending_ = false;
  std::string reset_settings_plugin_;
  std::string status_message_;
  static constexpr std::int64_t kMonitorsCacheMs = 1000;
  std::vector<MonitorInfo> monitors_;
  std::int64_t             monitors_ms_ = 0;
};

}  // namespace host
}  // namespace sm

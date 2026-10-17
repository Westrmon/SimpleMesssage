// SimpleMessage — 控制面板：六页（插件详情由独立的详情窗口承载）
#pragma once

#include <cstdint>
#include <memory>
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

  // 规则页：只读快照 + 两个动作。宿主保证返回的 RuleSet 与管线正在用的
  // 是同一个实例，因此这里的启用开关会立即生效。
  virtual std::shared_ptr<RuleSet> rules_snapshot() const = 0;
  virtual bool ReloadRules() = 0;
  virtual void SetRuleEnabled(const std::string& id, bool enabled) = 0;
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
  void DrawRulesPage();
  void DrawRoutingPage();
  void DrawLogPage();
  void DrawAboutPage();
  void DrawPluginDetail(const PluginInfo& info);

  void DrawBanners();
  void DrawTabBar();
  void DrawConfirmPopups();
  void DrawInstancePopups();
  void OpenAddInstancePopup(const std::string& type_id);

  void GoToPluginPage(const std::string& plugin_id);
  void OnLogSnapshot();
  void ExportNotifications(const std::vector<const OwnedNotification*>& items);
  std::string ui_state_path() const;

  UiPanelSetup setup_;
  UiState*     state_ = nullptr;

  std::int64_t selected_seq_ = 0;
  int          selected_rule_ = -1;
  std::string  rules_status_;
  std::string        expanded_plugin_;
  std::string        scroll_to_plugin_;
  bool               pending_tab_select_ = false;
  TextInputState     form_state_;
  TextInputState     notify_topic_state_;
  TextInputState     notify_text_state_;
  TextInputState     log_text_state_;
  TextInputState     instance_name_state_;
  TextInputState     add_instance_name_state_;
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

  bool        add_instance_pending_ = false;
  std::string add_instance_type_;
  std::string add_instance_name_;
  std::string add_instance_error_;

  bool        delete_instance_pending_ = false;
  std::string delete_instance_id_;
  std::string delete_instance_name_;
  std::string delete_instance_path_;
  std::string delete_instance_error_;

  std::string status_message_;
  static constexpr std::int64_t kMonitorsCacheMs = 1000;
  std::vector<MonitorInfo> monitors_;
  std::int64_t             monitors_ms_ = 0;
};

}  // namespace host
}  // namespace sm

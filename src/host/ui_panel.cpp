// SimpleMessage — 控制面板的骨架：横幅、页签、引用与关闭语义、确认弹窗
#include "host/ui_panel.h"

#include "imgui.h"

#include "host/strings.h"

namespace sm {
namespace host {

void LogConfigDiagnostics(const config::Diagnostics& diagnostics) {
  for (const config::Diagnostic& d : diagnostics) {
    sm::LogLevel level = sm::LogLevel::Info;
    if (d.severity == config::Diagnostic::Severity::Warn) level = sm::LogLevel::Warn;
    if (d.severity == config::Diagnostic::Severity::Error) level = sm::LogLevel::Error;
    log::Write(level, "host", d.message.c_str());
  }
}

UiPanel::UiPanel(UiPanelSetup setup, UiState* state) : setup_(setup), state_(state) {}

UiPanel::~UiPanel() = default;

bool UiPanel::open() const {
  return setup_.session != nullptr && setup_.session->panel_open();
}

void UiPanel::Open() {
  if (setup_.session == nullptr) return;
  setup_.session->SetPanelOpen(true);
}

void UiPanel::OpenAt(PanelPage page) {
  if (state_ != nullptr) {
    state_->last_page = page;
    pending_tab_select_ = true;
  }
  Open();
}

bool UiPanel::RequestClose() {
  if (setup_.session == nullptr || !setup_.session->panel_open()) return false;

  setup_.session->SetPanelOpen(false);

  if (state_ != nullptr && !state_->close_hint_shown && setup_.host != nullptr) {
    state_->close_hint_shown = true;
    setup_.host->NotifyCloseHint();
  }
  return true;
}

void UiPanel::OpenForViews() {
  if (setup_.session == nullptr) return;
  if (setup_.session->refs().views <= 0) return;
  Open();
}

PanelPage UiPanel::page() const {
  return (state_ != nullptr) ? state_->last_page : PanelPage::Overview;
}

void UiPanel::CaptureWindowRect() {
  if (state_ == nullptr || setup_.host == nullptr) return;
  int x = UiState::kUseSystemPosition;
  int y = UiState::kUseSystemPosition;
  int w = 0;
  int h = 0;
  setup_.host->CurrentWindowRect(&x, &y, &w, &h);
  state_->x = x;
  state_->y = y;
  state_->w = w;
  state_->h = h;
}

std::string UiPanel::ui_state_path() const {
  return (setup_.host != nullptr) ? UiStatePath(setup_.host->paths()) : std::string();
}

void UiPanel::Draw() {
  if (setup_.session == nullptr) return;

  if (!setup_.session->panel_open()) {
    if (setup_.session->refs().views > 0) DrawStillInUseNotice();
    return;
  }

  if (!modal_active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    RequestClose();
    return;
  }

  DrawBanners();
  DrawTabBar();
  DrawConfirmPopups();
}

bool UiPanel::modal_active() const {
  return clear_history_pending_ || reset_settings_pending_;
}

void UiPanel::DrawBanners() {
  if (setup_.host == nullptr) return;

  if (setup_.host->safe_mode()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
    ImGui::TextWrapped("%s", strings::kSafeModeBanner);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%s", strings::kSafeModeHint);
    ImGui::Separator();
  }

  if (setup_.host->config_unsaved()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", strings::kConfigSaveFailedBanner);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%s", strings::kConfigSaveFailedHint);
    ImGui::SameLine();
    if (ImGui::SmallButton(strings::kRetry)) setup_.host->RetryConfigSave();
    ImGui::Separator();
  }

  if (!status_message_.empty()) {
    ImGui::TextDisabled("%s", status_message_.c_str());
    ImGui::Separator();
  }
}

void UiPanel::DrawTabBar() {
  if (state_ == nullptr) return;

  if (!ImGui::BeginTabBar("##pages", ImGuiTabBarFlags_None)) return;

  static const char* const kNames[kPanelPageCount] = {
      strings::kPageOverview, strings::kPagePlugins, strings::kPageNotifications,
      strings::kPageRouting, strings::kPageLog,     strings::kPageAbout,
  };

  for (int i = 0; i < kPanelPageCount; ++i) {
    const PanelPage target = static_cast<PanelPage>(i);
    const bool select_this = pending_tab_select_ && (target == state_->last_page);
    const ImGuiTabItemFlags flags =
        select_this ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

    if (ImGui::BeginTabItem(kNames[i], nullptr, flags)) {
      if (!pending_tab_select_) state_->last_page = target;
      DrawFixedPage(target);
      ImGui::EndTabItem();
      if (select_this) pending_tab_select_ = false;
    }
  }

  DrawViewPages();
  ImGui::EndTabBar();
}

void UiPanel::DrawViewPages() {
  if (setup_.session == nullptr) return;

  const std::vector<UiSession::ViewRef> views = setup_.session->Views();
  SyncKnownViews(views);

  for (const UiSession::ViewRef& view : views) {
    bool visible = true;
    ImGui::PushID(view.plugin_id.c_str());
    const std::string label =
        (view.title.empty() ? view.plugin_id : view.title) + "###view";

    const bool select_this = (select_view_ == view.plugin_id);
    if (ImGui::BeginTabItem(label.c_str(), &visible,
                            select_this ? ImGuiTabItemFlags_SetSelected
                                        : ImGuiTabItemFlags_None)) {
      select_view_.clear();
      if (setup_.plugins != nullptr) setup_.plugins->InvokeDrawView(view.plugin_id);
      ImGui::EndTabItem();
    }
    ImGui::PopID();

    if (!visible) {
      setup_.session->CloseView(view.plugin_id);
      break;
    }
  }
}

void UiPanel::SyncKnownViews(const std::vector<UiSession::ViewRef>& views) {
  for (const UiSession::ViewRef& view : views) {
    bool known = false;
    for (const std::string& id : known_views_) {
      if (id == view.plugin_id) {
        known = true;
        break;
      }
    }
    if (!known) {
      known_views_.push_back(view.plugin_id);
      select_view_ = view.plugin_id;
    }
  }

  for (std::size_t i = 0; i < known_views_.size();) {
    bool alive = false;
    for (const UiSession::ViewRef& view : views) {
      if (view.plugin_id == known_views_[i]) {
        alive = true;
        break;
      }
    }
    if (alive) {
      ++i;
    } else {
      known_views_.erase(known_views_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
}

void UiPanel::DrawFixedPage(PanelPage target) {
  switch (target) {
    case PanelPage::Overview:      DrawOverviewPage(); break;
    case PanelPage::Plugins:       DrawPluginsPage(); break;
    case PanelPage::Notifications: DrawNotificationsPage(); break;
    case PanelPage::Routing:       DrawRoutingPage(); break;
    case PanelPage::Log:           DrawLogPage(); break;
    case PanelPage::About:         DrawAboutPage(); break;
  }
}

void UiPanel::DrawStillInUseNotice() {
  const std::string reason = setup_.session->AliveReason();
  ImGui::TextWrapped(strings::kStillInUseFormat, reason.empty() ? "?" : reason.c_str());
  ImGui::TextDisabled("%s", strings::kStillInUseHint);
  ImGui::Separator();
  if (ImGui::Button(strings::kOpenPanel)) Open();
}

void UiPanel::DrawConfirmPopups() {
  if (clear_history_pending_) {
    ImGui::OpenPopup(strings::kClearHistoryTitle);
    clear_history_pending_ = false;
  }
  if (reset_settings_pending_) {
    ImGui::OpenPopup(strings::kResetSettingsTitle);
    reset_settings_pending_ = false;
  }

  if (ImGui::BeginPopupModal(strings::kClearHistoryTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", strings::kClearHistoryBody);
    ImGui::Separator();
    if (ImGui::Button(strings::kConfirm)) {
      if (setup_.pipeline != nullptr) setup_.pipeline->ClearHistory();
      selected_seq_ = 0;
      status_message_ = strings::kHistoryCleared;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(strings::kCancel)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(strings::kResetSettingsTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(strings::kResetSettingsBodyFormat, reset_settings_plugin_.c_str());
    ImGui::Separator();
    if (ImGui::Button(strings::kConfirm)) {
      if (setup_.config != nullptr && !reset_settings_plugin_.empty()) {
        config::Diagnostics diagnostics;
        setup_.config->ResetPluginSettings(reset_settings_plugin_, &diagnostics);
        LogConfigDiagnostics(diagnostics);
        status_message_ = strings::kSettingsReset;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(strings::kCancel)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void UiPanel::GoToPluginPage(const std::string& plugin_id) {
  if (state_ == nullptr) return;
  state_->last_page = PanelPage::Plugins;
  pending_tab_select_ = true;
  scroll_to_plugin_ = plugin_id;
  expanded_plugin_ = plugin_id;
}

void UiPanel::OnLogSnapshot() {
  if (state_ != nullptr && state_->log.paused) return;

  const std::uint64_t version = log::Version();
  if (version == log_version_) return;
  log_version_ = version;

  log_entries_.resize(kLogSnapshotMax);
  const std::size_t count = log::Snapshot(log_entries_.data(), kLogSnapshotMax);
  log_entries_.resize(count);
  log_dropped_ = log::DroppedCount();
}

}  // namespace host
}  // namespace sm

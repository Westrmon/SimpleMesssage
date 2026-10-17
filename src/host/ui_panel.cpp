// SimpleMessage — 控制面板的骨架：横幅、页签、引用与关闭语义、确认弹窗
#include "host/ui_panel.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "host/strings.h"

namespace sm {
namespace host {

namespace {

std::string Fmt(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return std::string(buffer);
}

}  // namespace

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

  if (!setup_.session->panel_open()) return;

  if (!modal_active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    RequestClose();
    return;
  }

  DrawBanners();
  DrawTabBar();
  DrawConfirmPopups();
}

bool UiPanel::modal_active() const {
  return clear_history_pending_ || reset_settings_pending_ || add_instance_pending_ ||
         delete_instance_pending_ ||
         ImGui::IsPopupOpen(strings::kAddInstanceTitle) ||
         ImGui::IsPopupOpen(strings::kInstanceDeleteTitle);
}

void UiPanel::OpenAddInstancePopup(const std::string& type_id) {
  add_instance_type_ = type_id;
  add_instance_name_.clear();
  add_instance_error_.clear();
  add_instance_name_state_ = TextInputState{};
  add_instance_pending_ = true;
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
      strings::kPageRules,    strings::kPageRouting, strings::kPageLog,
      strings::kPageAbout,
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

  ImGui::EndTabBar();
}

void UiPanel::DrawFixedPage(PanelPage target) {
  switch (target) {
    case PanelPage::Overview:      DrawOverviewPage(); break;
    case PanelPage::Plugins:       DrawPluginsPage(); break;
    case PanelPage::Notifications: DrawNotificationsPage(); break;
    case PanelPage::Rules:         DrawRulesPage(); break;
    case PanelPage::Routing:       DrawRoutingPage(); break;
    case PanelPage::Log:           DrawLogPage(); break;
    case PanelPage::About:         DrawAboutPage(); break;
  }
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

  DrawInstancePopups();
}

void UiPanel::DrawInstancePopups() {
  if (setup_.plugins == nullptr) return;

  if (add_instance_pending_) {
    ImGui::OpenPopup(strings::kAddInstanceTitle);
    add_instance_pending_ = false;
  }
  if (delete_instance_pending_) {
    ImGui::OpenPopup(strings::kInstanceDeleteTitle);
    delete_instance_pending_ = false;
  }

  const bool readonly = setup_.config == nullptr || setup_.config->read_only() ||
                        setup_.config->SchemaTooNewDetected();

  if (ImGui::BeginPopupModal(strings::kAddInstanceTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    std::vector<PluginTypeInfo> types = setup_.plugins->SnapshotTypes();
    std::vector<const PluginTypeInfo*> candidates;
    for (const PluginTypeInfo& type : types) {
      if (type.kind == sm::PluginKind::Source && type.can_add_instance) {
        candidates.push_back(&type);
      }
    }

    if (readonly) {
      ImGui::TextWrapped("%s", strings::kAddInstanceReadonly);
    } else if (candidates.empty()) {
      ImGui::TextWrapped("%s", strings::kAddInstanceNoTypes);
    } else {
      ImGui::TextUnformatted(strings::kAddInstanceTypeLabel);
      for (const PluginTypeInfo* type : candidates) {
        ImGui::PushID(type->type_id.c_str());
        const std::string label =
            Fmt("%s  ·  %s  ·  %s", type->name.c_str(), type->type_id.c_str(),
                Fmt(strings::kAddInstanceCountFormat,
                    static_cast<int>(type->instance_count)).c_str());
        const bool selected = (add_instance_type_ == type->type_id);
        if (ImGui::Selectable(label.c_str(), selected)) {
          add_instance_type_ = type->type_id;
          add_instance_error_.clear();
        }
        ImGui::PopID();
      }

      ImGui::Separator();
      ImGui::TextUnformatted(strings::kAddInstanceNameLabel);
      InputTextStdString("##add_instance_name", "add-instance-name",
                         &add_instance_name_, 96, &add_instance_name_state_);
      ImGui::TextDisabled("%s", strings::kAddInstanceNameHint);
    }

    if (!add_instance_error_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.40f, 1.0f));
      ImGui::TextWrapped("%s", add_instance_error_.c_str());
      ImGui::PopStyleColor();
    }

    ImGui::Separator();
    const bool can_confirm = !readonly && !add_instance_type_.empty();
    if (!can_confirm) ImGui::BeginDisabled();
    if (ImGui::Button(strings::kConfirm)) {
      const AddInstanceResult result =
          setup_.plugins->AddInstance(add_instance_type_, add_instance_name_);
      if (result.ok) {
        expanded_plugin_ = result.instance_id;
        scroll_to_plugin_ = result.instance_id;
        status_message_ = Fmt(strings::kAddInstanceOkFormat, result.instance_id.c_str());
        add_instance_type_.clear();
        add_instance_name_.clear();
        add_instance_error_.clear();
        add_instance_name_state_ = TextInputState{};
        ImGui::CloseCurrentPopup();
      } else {
        add_instance_error_ =
            Fmt(strings::kAddInstanceFailedFormat, result.error.c_str());
      }
    }
    if (!can_confirm) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(strings::kCancel)) {
      add_instance_error_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(strings::kInstanceDeleteTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(strings::kInstanceDeleteBodyFormat, delete_instance_name_.c_str(),
                       delete_instance_id_.c_str(), delete_instance_path_.c_str());
    if (!delete_instance_error_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.40f, 1.0f));
      ImGui::TextWrapped("%s", delete_instance_error_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (readonly) ImGui::BeginDisabled();
    if (ImGui::Button(strings::kConfirm)) {
      std::string error;
      if (setup_.plugins->RemoveInstance(delete_instance_id_, &error)) {
        if (expanded_plugin_ == delete_instance_id_) expanded_plugin_.clear();
        if (scroll_to_plugin_ == delete_instance_id_) scroll_to_plugin_.clear();
        status_message_ =
            Fmt(strings::kInstanceDeletedFormat, delete_instance_id_.c_str());
        delete_instance_error_.clear();
        ImGui::CloseCurrentPopup();
      } else {
        delete_instance_error_ =
            Fmt(strings::kInstanceDeleteFailedFormat,
                error.empty() ? "未知原因" : error.c_str());
      }
    }
    if (readonly) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(strings::kCancel)) {
      delete_instance_error_.clear();
      ImGui::CloseCurrentPopup();
    }
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

// SimpleMessage — 控制面板的七页
#include "host/ui_panel.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "platform/dialog.h"
#include "platform/platform.h"
#include "platform/system.h"
#include "sm/version.h"
#include "sm/version_range.h"

#include "host/internal_source.h"
#include "host/monitor.h"
#include "host/notify_view.h"
#include "host/strings.h"
#include "host/util.h"

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

const ImVec4 kColorOk(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kColorWarn(1.00f, 0.80f, 0.30f, 1.0f);
const ImVec4 kColorBad(1.00f, 0.45f, 0.40f, 1.0f);
const ImVec4 kColorDim(0.62f, 0.62f, 0.62f, 1.0f);

void AddUnique(std::vector<std::string>* list, const std::string& value) {
  if (value.empty()) return;
  if (std::find(list->begin(), list->end(), value) != list->end()) return;
  list->push_back(value);
}

std::string DisplayName(const std::vector<PluginInfo>& plugins, const std::string& id) {
  for (const PluginInfo& info : plugins) {
    if (info.id != id) continue;
    return info.name.empty() ? info.id : info.name;
  }
  return id;
}

const PluginInfo* FindPlugin(const std::vector<PluginInfo>& plugins, const std::string& id) {
  for (const PluginInfo& info : plugins) {
    if (info.id == id) return &info;
  }
  return nullptr;
}

bool ReadOnlyMode(const UiPanelSetup& setup) {
  if (setup.config == nullptr || setup.config->read_only() ||
      setup.config->SchemaTooNewDetected()) {
    return true;
  }
  return setup.host != nullptr && setup.host->safe_mode();
}

std::string PluginConfigPath(const config::Paths& paths, const std::string& id) {
  return util::path_join(paths.plugin_config_dir, id + ".toml");
}

std::string DurationText(std::int64_t ms) {
  char text[64];
  util::format_duration(ms < 0 ? 0 : ms, text, sizeof(text));
  return text;
}

std::string ShortTime(std::int64_t unix_ms) {
  char text[32];
  util::format_short_time(unix_ms, util::now_unix_ms(), text, sizeof(text));
  return text;
}

const char* KindText(sm::PluginKind kind) {
  return (kind == sm::PluginKind::Notifier) ? strings::kKindNotifier : strings::kKindSource;
}

void StateText(const PluginInfo& info, std::string* out, ImVec4* color) {
  const char* const reason =
      info.reason.empty() ? strings::kStateNoReason : info.reason.c_str();
  switch (info.state) {
    case PluginState::Running:      *out = strings::kStateRunning;      *color = kColorOk;   return;
    case PluginState::Loaded:       *out = strings::kStateLoaded;       *color = kColorOk;   return;
    case PluginState::Initialized:  *out = strings::kStateInitialized;  *color = kColorOk;   return;
    case PluginState::Discovered:   *out = strings::kStateNotLoaded;    *color = kColorDim;  return;
    case PluginState::Stopped:      *out = strings::kStateStopped;      *color = kColorDim;  return;
    case PluginState::Unloaded:     *out = strings::kStateUnloaded;     *color = kColorDim;  return;
    case PluginState::InitializeFailed:
      *out = Fmt(strings::kStateInitFailedFormat, reason);
      *color = kColorWarn;
      return;
    case PluginState::Disabled:
      *out = Fmt(strings::kStateDisabledFormat, reason);
      *color = kColorWarn;
      return;
    case PluginState::Rejected:
      *out = Fmt(strings::kStateRejectedFormat, reason);
      *color = kColorBad;
      return;
  }
  *out = strings::kStateNotLoaded;
  *color = kColorDim;
}

std::string ActivityText(const PluginInfo& info) {
  if (info.kind == sm::PluginKind::Notifier) {
    if (info.last_notify_unix_ms == 0) return strings::kActivityNone;
    return Fmt(strings::kActivityNotifyFormat, ShortTime(info.last_notify_unix_ms).c_str(),
               info.last_notify_crashed ? strings::kActivityCrashed : strings::kActivityStarted);
  }
  if (info.last_start_unix_ms == 0) return strings::kActivityNone;
  const char* const result = info.last_start_crashed
                                 ? strings::kActivityCrashed
                                 : strings::StartResultText(info.last_start_result);
  return Fmt(strings::kActivityStartFormat, ShortTime(info.last_start_unix_ms).c_str(), result);
}

const char* BuildTypeText() {
#if defined(NDEBUG)
  return "Release";
#else
  return "Debug";
#endif
}

std::string CompilerText() {
#if defined(_MSC_VER)
  return Fmt("MSVC %d.%02d (%d)", _MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER);
#elif defined(__clang__)
  return Fmt("Clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
  return Fmt("GCC %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
  return "未知";
#endif
}

const char* PlatformText() { return platform::architecture_name(); }

void LabelRow(const char* label, const std::string& value) {
  ImGui::Text(strings::kLabelValueFormat, label, value.c_str());
}

void PathRow(const char* label, const std::string& path, IPanelHost* host) {
  if (path.empty()) {
    ImGui::Text(strings::kLabelValueFormat, label, strings::kNone);
    return;
  }
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::TextUnformatted(path.c_str());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", path.c_str());
  ImGui::SameLine();
  ImGui::PushID(label);
  if (ImGui::SmallButton(strings::kOpen) && host != nullptr) host->OpenInShell(path);
  ImGui::PopID();
}

std::string AskExportPath(const std::string& default_dir) {
  platform::SaveFileDialogOptions options;
  options.default_name = "simplemessage-notifications.jsonl";
  options.initial_directory = default_dir;
  options.filter_label = "JSON Lines (*.jsonl)";
  options.filter_extension = "jsonl";
  options.all_files_label = "所有文件 (*.*)";
  return platform::ShowSaveFileDialog(options);
}

}  // namespace

void UiPanel::DrawOverviewPage() {
  std::vector<PluginInfo> plugins;
  if (setup_.plugins != nullptr) plugins = setup_.plugins->Snapshot();
  std::vector<Scheduler::PluginSchedule> schedules;
  if (setup_.scheduler != nullptr) schedules = setup_.scheduler->Snapshot();

  ImGui::SeparatorText(strings::kSecVersion);
  ImGui::Text(strings::kHostVersionFormat, SM_VERSION_STRING, static_cast<int>(SM_API_VERSION));

  ImGui::SeparatorText(strings::kSecRuntime);
  if (setup_.host != nullptr && setup_.host->started_monotonic_ms() > 0) {
    ImGui::Text(strings::kUptimeFormat,
                DurationText(platform::monotonic_ms() - setup_.host->started_monotonic_ms()).c_str());
  }
  if (setup_.host != nullptr) {
    ImGui::TextUnformatted(setup_.host->owns_single_instance() ? strings::kSingleInstanceOwner
                                                              : strings::kSingleInstanceShared);
  }
  if (setup_.scheduler != nullptr) {
    std::string busy;
    if (setup_.scheduler->IsAnyExecuting(&busy)) {
      int timeouts = 0;
      for (const Scheduler::PluginSchedule& schedule : schedules) {
        if (schedule.id == busy) {
          timeouts = schedule.consecutive_timeouts;
          break;
        }
      }
      ImGui::Text(strings::kSchedulerStuckFormat, DisplayName(plugins, busy).c_str(), timeouts);
    } else {
      ImGui::TextUnformatted(setup_.scheduler->paused() ? strings::kSchedulerPaused
                                                        : strings::kSchedulerRunning);
    }
  }

  ImGui::SeparatorText(strings::kSecPluginStats);
  if (setup_.plugins != nullptr) {
    const PluginManager::Stats stats = setup_.plugins->SnapshotStats();
    ImGui::Text(strings::kStatLoadedFormat, stats.loaded);
    ImGui::SameLine();
    ImGui::Text(strings::kStatEnabledFormat, stats.enabled);
    ImGui::SameLine();
    ImGui::Text(strings::kStatDisabledFormat, stats.disabled);
    ImGui::SameLine();
    ImGui::Text(strings::kStatRejectedFormat, stats.rejected);
  }

  ImGui::SeparatorText(strings::kSecNotifyStats);
  if (setup_.pipeline != nullptr) {
    const NotificationPipeline::Stats stats = setup_.pipeline->SnapshotStats();

    char produced_text[256];
    char queued_text[256];
    char dropped_text[256];
    char undelivered_text[256];
    char task_queue_text[256];

    std::snprintf(produced_text, sizeof(produced_text),
                  strings::kStatProducedFormat,
                  static_cast<long long>(stats.produced));
    std::snprintf(queued_text, sizeof(queued_text),
                  strings::kStatQueuedFormat,
                  static_cast<long long>(setup_.pipeline->queued()));
    std::snprintf(dropped_text, sizeof(dropped_text),
                  strings::kStatDroppedFormat,
                  static_cast<long long>(stats.dropped));
    std::snprintf(undelivered_text, sizeof(undelivered_text),
                  strings::kStatUndeliveredFormat,
                  static_cast<long long>(stats.undelivered));

    const float content_right_x =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

    bool has_previous_item = false;

    const auto draw_wrapped = [&](const char *text)
    {
      if (has_previous_item)
      {
        const float next_item_x =
            ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
        const float text_width = ImGui::CalcTextSize(text).x;

        if (next_item_x + text_width <= content_right_x)
        {
          ImGui::SameLine();
        }
      }

      ImGui::TextWrapped("%s", text);
      has_previous_item = true;
    };

    draw_wrapped(produced_text);
    draw_wrapped(queued_text);
    draw_wrapped(dropped_text);
    draw_wrapped(undelivered_text);

    if (setup_.plugins != nullptr)
    {
      std::snprintf(task_queue_text, sizeof(task_queue_text),
                    strings::kStatTaskQueueFormat,
                    static_cast<int>(
                        setup_.plugins->main_thread_queue_size()));

      draw_wrapped(task_queue_text);
    }
    if (stats.dropped > 0) ImGui::TextDisabled("%s", strings::kDroppedHint);
    if (stats.undelivered > 0) ImGui::TextDisabled("%s", strings::kUndeliveredHint);
  }

  ImGui::SeparatorText(strings::kSecErrors);
  const PluginInfo* crashed = nullptr;
  const PluginInfo* init_failed = nullptr;
  const PluginInfo* auto_disabled = nullptr;
  const PluginInfo* start_failed = nullptr;
  for (const PluginInfo& info : plugins) {
    if (info.last_start_crashed || info.last_notify_crashed) {
      const std::int64_t when = std::max(info.last_start_unix_ms, info.last_notify_unix_ms);
      if (crashed == nullptr ||
          when > std::max(crashed->last_start_unix_ms, crashed->last_notify_unix_ms)) {
        crashed = &info;
      }
    }
    if (info.state == PluginState::InitializeFailed && init_failed == nullptr) init_failed = &info;
    if (info.state == PluginState::Disabled && info.auto_disabled &&
        auto_disabled == nullptr) {
      auto_disabled = &info;
    }
    if (info.state != PluginState::Disabled && !info.last_start_crashed &&
        info.last_start_result == sm::StartResult::Error &&
        (start_failed == nullptr || info.last_start_unix_ms > start_failed->last_start_unix_ms)) {
      start_failed = &info;
    }
  }

  if (crashed == nullptr && init_failed == nullptr && auto_disabled == nullptr &&
      start_failed == nullptr) {
    ImGui::TextDisabled("%s", strings::kNoErrors);
    return;
  }
  if (crashed != nullptr) {
    ImGui::Text(strings::kLastCrashFormat,
                DisplayName(plugins, crashed->id).c_str(),
                ShortTime(std::max(crashed->last_start_unix_ms, crashed->last_notify_unix_ms))
                    .c_str());
    ImGui::SameLine();
    ImGui::PushID("crash");
    if (ImGui::SmallButton(strings::kGoToPlugin)) GoToPluginPage(crashed->id);
    ImGui::PopID();
  }
  if (auto_disabled != nullptr) {
    ImGui::Text(strings::kLastAutoDisabledFormat,
                DisplayName(plugins, auto_disabled->id).c_str(),
                auto_disabled->reason.empty() ? strings::kStateNoReason
                                              : auto_disabled->reason.c_str());
    ImGui::SameLine();
    ImGui::PushID("auto_disabled");
    if (ImGui::SmallButton(strings::kGoToPlugin)) GoToPluginPage(auto_disabled->id);
    ImGui::PopID();
  }
  if (init_failed != nullptr) {
    ImGui::Text(strings::kLastInitFailedFormat,
                DisplayName(plugins, init_failed->id).c_str(),
                init_failed->reason.empty() ? strings::kStateNoReason
                                            : init_failed->reason.c_str());
    ImGui::SameLine();
    ImGui::PushID("init_failed");
    if (ImGui::SmallButton(strings::kGoToPlugin)) GoToPluginPage(init_failed->id);
    ImGui::PopID();
  }
  if (start_failed != nullptr) {
    const Scheduler::PluginSchedule* schedule = nullptr;
    for (const Scheduler::PluginSchedule& candidate : schedules) {
      if (candidate.id == start_failed->id) {
        schedule = &candidate;
        break;
      }
    }
    const int failures = (schedule != nullptr) ? schedule->consecutive_failures : 0;
    const std::string retry =
        (schedule != nullptr && schedule->scheduled)
            ? Fmt(strings::kRetryInFormat,
                  DurationText(schedule->next_due_in_ms).c_str())
            : std::string(strings::kStartFailedNoRetry);
    ImGui::Text(strings::kLastStartFailedFormat,
                DisplayName(plugins, start_failed->id).c_str(), failures, retry.c_str());
    ImGui::SameLine();
    ImGui::PushID("start_failed");
    if (ImGui::SmallButton(strings::kGoToPlugin)) GoToPluginPage(start_failed->id);
    ImGui::PopID();
    if (schedule != nullptr && !schedule->last_error.empty()) {
      ImGui::TextDisabled("%s", schedule->last_error.c_str());
    }
  }
}

void UiPanel::DrawPluginsPage() {
  if (setup_.plugins == nullptr) return;

  const bool readonly = ReadOnlyMode(setup_);
  std::vector<PluginInfo> rows = setup_.plugins->Snapshot();
  rows.push_back(InternalSource::Info());

  const auto set_enabled = [this](const PluginInfo& info, bool enabled) {
    if (InternalSource::IsInternalId(info.id)) return;
    if (enabled) {
      setup_.plugins->Enable(info.id);
    } else {
      setup_.plugins->Disable(info.id);
    }
  };

  // 批量操作只作用于真实实例：类型行、被拒绝的实例与宿主内建来源都跳过。
  const auto set_all = [&rows, &set_enabled, readonly](bool enabled) {
    if (readonly) return;
    for (const PluginInfo& info : rows) {
      if (!info.is_instance || info.state == PluginState::Rejected) continue;
      if (InternalSource::IsInternalId(info.id)) continue;
      if (info.enabled == enabled) continue;
      set_enabled(info, enabled);
    }
  };

  if (readonly) ImGui::BeginDisabled();
  if (ImGui::Button(strings::kAddInstance)) OpenAddInstancePopup(std::string());
  if (readonly) ImGui::EndDisabled();

  ImGui::SameLine();
  const bool safe_mode = setup_.host != nullptr && setup_.host->safe_mode();
  if (safe_mode) ImGui::BeginDisabled();
  const bool reload_clicked = ImGui::Button(strings::kReloadPlugins);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("%s", strings::kReloadPluginsHint);
  }
  if (safe_mode) ImGui::EndDisabled();
  if (reload_clicked) {
    const ReloadResult result = setup_.plugins->ReloadPlugins();
    if (!result.ok) {
      status_message_ = Fmt(strings::kReloadPluginsFailedFormat, result.error.c_str());
    } else if (result.added == 0 && result.recovered == 0 && result.removed == 0) {
      status_message_ = strings::kReloadPluginsNoChange;
    } else {
      status_message_ = Fmt(strings::kReloadPluginsOkFormat, result.scanned, result.added,
                            result.accepted, result.rejected, result.recovered,
                            result.removed, result.instances_loaded);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button(strings::kBulkEnableAll)) set_all(true);
  ImGui::SameLine();
  if (ImGui::Button(strings::kBulkDisableAll)) set_all(false);

  if (readonly) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", strings::kInstanceReadonlyHint);
  }

  if (rows.size() == 1) ImGui::TextDisabled("%s", strings::kNoPlugins);

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  const ImVec2 size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f);

  if (ImGui::BeginTable("##plugins", 7, flags, size)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(strings::kColEnabled, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColName, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(strings::kColId, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColKind, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColVersion, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColState, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(strings::kColActivity, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const PluginInfo& info : rows) {
      ImGui::PushID(info.id.c_str());
      const bool expanded = (expanded_plugin_ == info.id);
      const bool scroll_here = (scroll_to_plugin_ == info.id);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      bool enabled = info.enabled;
      const bool is_internal = InternalSource::IsInternalId(info.id);
      const bool can_toggle = !readonly && !is_internal && info.is_instance &&
                              info.state != PluginState::Rejected;
      if (!can_toggle) ImGui::BeginDisabled();
      if (ImGui::Checkbox("##enabled", &enabled) && can_toggle) set_enabled(info, enabled);
      if (!can_toggle) ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(1);
      const std::string node_label =
          (info.name.empty() ? info.id : info.name) + "###plugin";
      ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
      const bool open_now =
          ImGui::TreeNodeEx(node_label.c_str(), ImGuiTreeNodeFlags_SpanAllColumns);
      if (open_now != expanded) expanded_plugin_ = open_now ? info.id : std::string();
      if (open_now) ImGui::TreePop();

      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(info.id.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(KindText(info.kind));
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(info.version.c_str());
      ImGui::TableSetColumnIndex(5);
      std::string state_text;
      ImVec4 state_color;
      StateText(info, &state_text, &state_color);
      ImGui::TextColored(state_color, "%s", state_text.c_str());
      ImGui::TableSetColumnIndex(6);
      const std::string activity = ActivityText(info);
      ImGui::TextDisabled("%s", activity.c_str());
      if (scroll_here) {
        ImGui::SetScrollHereY(0.5f);
        scroll_to_plugin_.clear();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (expanded_plugin_.empty()) return;
  const PluginInfo* const selected = FindPlugin(rows, expanded_plugin_);
  if (selected == nullptr) {
    expanded_plugin_.clear();
    return;
  }
  DrawPluginDetail(*selected);
}

void UiPanel::DrawPluginDetail(const PluginInfo& info) {
  const bool is_internal = InternalSource::IsInternalId(info.id);
  const bool readonly = ReadOnlyMode(setup_);

  ImGui::Separator();
  ImGui::TextUnformatted(info.name.empty() ? info.id.c_str() : info.name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", info.id.c_str());

  const float detail_height = ImGui::GetTextLineHeightWithSpacing() * 16.0f;
  if (ImGui::BeginChild("##plugin_detail", ImVec2(0.0f, detail_height),
                        ImGuiChildFlags_Borders)) {
    ImGui::PushTextWrapPos(0.0f);
    if (!info.description.empty()) ImGui::TextWrapped("%s", info.description.c_str());
    if (!info.author.empty()) ImGui::TextDisabled(strings::kAuthorFormat, info.author.c_str());
    if (!info.host_range.empty()) {
      const char* error = nullptr;
      const bool satisfied =
          sm::version_satisfies(SM_VERSION_STRING, info.host_range.c_str(), &error);
      ImGui::TextDisabled(strings::kHostRangeFormat, info.host_range.c_str(),
                          satisfied ? strings::kHostRangeOk : strings::kHostRangeBad);
      if (!satisfied && error != nullptr) ImGui::TextDisabled("%s", error);
    }
    if (!is_internal && !info.type_id.empty()) {
      ImGui::Text(strings::kInstanceTypeFormat, info.type_name.c_str(),
                  info.type_id.c_str());
    }
    if (info.is_instance) {
      ImGui::Text(strings::kInstanceIdFormat, info.id.c_str());
      ImGui::Text(strings::kInstanceOrdinalFormat,
                  static_cast<int>(info.instance_ordinal),
                  static_cast<int>(info.instance_count));
    } else if (!is_internal) {
      ImGui::TextWrapped("%s", info.is_rejected_type ? strings::kInstanceRejectedHint
                                                     : strings::kInstanceNone);
      if (!info.is_rejected_type) ImGui::TextDisabled("%s", strings::kInstanceTypeRowHint);
    }
    if (!info.reason.empty()) ImGui::TextWrapped(strings::kReasonFormat, info.reason.c_str());
    ImGui::PopTextWrapPos();

    if (info.state == PluginState::Disabled && !is_internal) {
      if (!readonly) {
        if (ImGui::Button(strings::kReEnable)) setup_.plugins->Enable(info.id);
        ImGui::SameLine();
      }
      ImGui::TextDisabled("%s", strings::kReEnableHint);
    }
    if (info.state == PluginState::InitializeFailed) {
      if (!readonly) {
        if (ImGui::Button(strings::kRetry)) setup_.plugins->RetryInitialize(info.id);
        ImGui::SameLine();
      }
    }

    if (info.is_instance) {
      // ---- 实例名称 ----
      ImGui::SeparatorText(strings::kInstanceNameLabel);
      if (readonly) ImGui::BeginDisabled();
      std::string name = info.custom_name;
      if (InputTextStdString("##instance_name", info.id.c_str(), &name, 96,
                             &instance_name_state_)) {
        setup_.plugins->RenameInstance(info.id, name);
      }
      if (readonly) ImGui::EndDisabled();
      ImGui::TextDisabled("%s", readonly ? strings::kInstanceReadonlyHint
                                         : strings::kInstanceNameHint);

      // ---- 设置 ----
      ImGui::SeparatorText(strings::kSecSettings);
      if (info.state == PluginState::Rejected) {
        ImGui::TextWrapped("%s", strings::kInstanceRejectedHint);
      } else if (info.setting_count == 0) {
        ImGui::TextDisabled("%s", strings::kNoSettings);
      } else if (info.settings.data == nullptr) {
        std::string state_text;
        ImVec4 state_color;
        StateText(info, &state_text, &state_color);
        ImGui::TextWrapped(strings::kSettingsUnavailableFormat, state_text.c_str());
      } else {
        config::Diagnostics diagnostics;
        config::PluginSettings& settings =
            setup_.config->EnsurePluginSettings(info.id, info.settings, &diagnostics);
        LogConfigDiagnostics(diagnostics);
        if (settings.schema_too_new()) {
          ImGui::TextWrapped("%s", strings::kSettingSchemaTooNew);
        } else {
          ImGui::TextDisabled("%s", strings::kSettingDirtyHint);
          DrawSettingsForm(info.id.c_str(), settings, info.settings, &form_state_,
                           setup_.config->PathsRef().config_dir.c_str());
        }
      }

      if (setup_.config != nullptr) {
        ImGui::TextDisabled(strings::kConfigPathFormat,
                            PluginConfigPath(setup_.config->PathsRef(), info.id).c_str());
        if (info.setting_count > 0 && !readonly &&
            info.state != PluginState::Rejected) {
          if (ImGui::Button(strings::kResetSettings)) {
            reset_settings_pending_ = true;
            reset_settings_plugin_ = info.id;
          }
        }
      }

      // ---- 删除实例 ----
      if (!is_internal && info.kind == sm::PluginKind::Source &&
          info.can_delete_instance) {
        ImGui::Separator();
        if (readonly) ImGui::BeginDisabled();
        if (ImGui::Button(strings::kInstanceDelete)) {
          delete_instance_pending_ = true;
          delete_instance_id_ = info.id;
          delete_instance_name_ = info.name;
          delete_instance_path_ = info.settings_path;
          delete_instance_error_.clear();
        }
        if (readonly) ImGui::EndDisabled();
      }
    } else if (!is_internal) {
      // ---- 零实例 / 被拒绝的类型行 ----
      if (!info.path.empty()) {
        ImGui::TextDisabled("%s", info.path.c_str());
      }
      if (!info.is_rejected_type) {
        ImGui::Separator();
        if (readonly) ImGui::BeginDisabled();
        if (ImGui::Button(strings::kInstanceAddForType)) {
          OpenAddInstancePopup(info.type_id);
        }
        if (readonly) ImGui::EndDisabled();
      }
    } else {
      // 宿主内建来源不是插件类型，没有设置也没有实例。
      ImGui::SeparatorText(strings::kSecSettings);
      ImGui::TextDisabled("%s", strings::kNoSettings);
    }
  }
  ImGui::EndChild();
}

void UiPanel::DrawNotificationsPage() {
  if (state_ == nullptr || setup_.pipeline == nullptr) return;
  NotifyFilter& filter = state_->notify;

  ImGui::SeparatorText(strings::kSecNotifySurface);
  if (setup_.config != nullptr) {
    ImGui::PushID("notify_surface");

    const config::NotifyWindowConfig& want = setup_.config->Data().notify_window;
    const bool want_primary = want.monitor.empty() || want.monitor == "primary";

    const std::int64_t now = platform::monotonic_ms();
    if (monitors_ms_ == 0 || now - monitors_ms_ >= kMonitorsCacheMs) {
      monitors_ = EnumerateMonitors();
      monitors_ms_ = now;
    }

    bool want_known = want_primary;
    for (const MonitorInfo& monitor : monitors_) {
      if (!want_primary && monitor.device == want.monitor) want_known = true;
    }

    std::vector<std::string> labels;
    std::vector<std::string> devices;
    labels.push_back(strings::kNotifyMonitorPrimary);
    devices.push_back("primary");
    for (const MonitorInfo& monitor : monitors_) {
      char text[192];
      std::snprintf(text, sizeof(text), strings::kNotifyMonitorFormat, monitor.device.c_str(),
                    monitor.work.x, monitor.work.y, monitor.work.width, monitor.work.height);
      labels.push_back(text);
      devices.push_back(monitor.device);
    }
    if (!want_known) {
      char text[192];
      std::snprintf(text, sizeof(text), strings::kNotifyMonitorMissingFormat, want.monitor.c_str());
      labels.push_back(text);
      devices.push_back(want.monitor);
    }
    const std::string current = want_primary ? "primary" : want.monitor;
    std::size_t current_index = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
      if (devices[i] == current) current_index = i;
    }

    ImGui::TextUnformatted(strings::kNotifyMonitor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::BeginCombo("##notify_monitor", labels[current_index].c_str())) {
      for (std::size_t i = 0; i < labels.size(); ++i) {
        if (ImGui::Selectable(labels[i].c_str(), i == current_index)) {
          setup_.config->SetNotifyWindow(devices[i], want.corner);
        }
      }
      ImGui::EndCombo();
    }

    ImGui::TextUnformatted(strings::kNotifyPosition);
    ImGui::SameLine();
    struct CornerChoice {
      const char*          label;
      config::NotifyCorner corner;
    };
    static const CornerChoice kCorners[] = {
        {strings::kNotifyCornerTopLeft, config::NotifyCorner::kTopLeft},
        {strings::kNotifyCornerTopRight, config::NotifyCorner::kTopRight},
        {strings::kNotifyCornerBottomLeft, config::NotifyCorner::kBottomLeft},
        {strings::kNotifyCornerBottomRight, config::NotifyCorner::kBottomRight},
    };
    for (std::size_t i = 0; i < 4; ++i) {
      if (i > 0) ImGui::SameLine();
      if (ImGui::RadioButton(kCorners[i].label, want.corner == kCorners[i].corner)) {
        setup_.config->SetNotifyWindow(want.monitor, kCorners[i].corner);
      }
    }

    if (!want_known){
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", strings::kNotifyMonitorMissingHint);
      ImGui::PopStyleColor();
    }
    ImGui::PopID();
  }

  if (setup_.config != nullptr) {
    ImGui::SeparatorText(strings::kSecRuleSave);
    const bool readonly = ReadOnlyMode(setup_);
    bool allowed = setup_.config->App().allow_rule_save;
    if (readonly) ImGui::BeginDisabled();
    if (ImGui::Checkbox(strings::kAllowRuleSave, &allowed)) {
      setup_.config->SetAllowRuleSave(allowed);
      if (setup_.pipeline != nullptr) {
        setup_.pipeline->SetRuleSaveEnabled(
            allowed && (setup_.host == nullptr || !setup_.host->safe_mode()));
      }
    }
    if (readonly) ImGui::EndDisabled();
  }

  ImGui::TextUnformatted(strings::kFilterLevel);
  ImGui::SameLine();
  for (int i = 0; i < kNotifyLevelCount; ++i) {
    bool on = filter.levels.Has(kNotifyLevels[i]);
    ImGui::PushID(i);
    if (ImGui::Checkbox(strings::LevelText(kNotifyLevels[i]), &on)) filter.levels.Set(i, on);
    ImGui::PopID();
    if (i + 1 < kNotifyLevelCount) ImGui::SameLine();
  }

  const std::vector<const OwnedNotification*> all = setup_.pipeline->History();
  AddUnique(&notify_sources_, kInternalSourceId);
  for (const OwnedNotification* n : all) {
    if (n != nullptr) AddUnique(&notify_sources_, n->source_plugin);
  }

  ImGui::SetNextItemWidth(160.0f);
  const char* const source_label =
      filter.source.empty() ? strings::kFilterSourceAll : filter.source.c_str();
  if (ImGui::BeginCombo(strings::kFilterSource, source_label)) {
    if (ImGui::Selectable(strings::kFilterSourceAll, filter.source.empty())) filter.source.clear();
    for (const std::string& source : notify_sources_) {
      if (ImGui::Selectable(source.c_str(), filter.source == source)) filter.source = source;
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  ImGui::TextUnformatted(strings::kFilterTopic);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  InputTextStdString("##filter_topic", "notify_topic", &filter.topic, 128, &notify_topic_state_);
  ImGui::SameLine();
  ImGui::TextUnformatted(strings::kFilterText);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  InputTextStdString("##filter_text", "notify_text", &filter.text, 128, &notify_text_state_);
  ImGui::SameLine();
  if (ImGui::Button(strings::kFilterClear)) filter = NotifyFilter{};

  std::vector<const OwnedNotification*> filtered;
  filtered.reserve(all.size());
  for (const OwnedNotification* n : all) {
    if (n != nullptr && MatchesNotifyFilter(filter, *n)) filtered.push_back(n);
  }

  ImGui::TextDisabled(strings::kFilteredFormat, static_cast<int>(filtered.size()),
                      static_cast<int>(all.size()));
  if (ImGui::Button(strings::kExportFiltered)) ExportNotifications(filtered);
  ImGui::SameLine();
  if (ImGui::Button(strings::kExportAll)) ExportNotifications(all);
  ImGui::SameLine();
  if (ImGui::Button(strings::kClearHistory)) clear_history_pending_ = true;

  if (!export_message_.empty()) {
    if (export_failed_) {
      ImGui::TextColored(kColorBad, "%s", export_message_.c_str());
    } else {
      ImGui::TextDisabled("%s", export_message_.c_str());
    }
  }

  if (all.empty()) {
    ImGui::TextDisabled("%s", strings::kHistoryEmpty);
    return;
  }

  const std::int64_t clicked = DrawNotificationList("##notifications", filtered, selected_seq_);
  if (clicked != 0) selected_seq_ = clicked;

  const OwnedNotification* const selected =
      (selected_seq_ != 0) ? setup_.pipeline->FindBySeq(selected_seq_) : nullptr;
  if (selected == nullptr) {
    ImGui::TextDisabled("%s", strings::kSelectRowHint);
    return;
  }
  ImGui::Separator();
  DrawNotificationDetail("##detail", *selected, setup_.router, setup_.actions);
}

void UiPanel::ExportNotifications(const std::vector<const OwnedNotification*>& items) {
  const std::string dir =
      (setup_.host != nullptr) ? setup_.host->paths().data_dir : std::string();
  const std::string path = AskExportPath(dir);
  if (path.empty()) return;

  std::string error;
  if (WriteNotificationsJsonl(path, items, &error)) {
    export_failed_ = false;
    export_message_ = Fmt(strings::kExportOkFormat, static_cast<int>(items.size()), path.c_str());
  } else {
    export_failed_ = true;
    export_message_ = Fmt(strings::kExportFailedFormat, path.c_str(), error.c_str());
  }
}

void UiPanel::DrawRulesPage() {
  if (setup_.host == nullptr || setup_.config == nullptr) return;

  ImGui::TextDisabled(strings::kRulesFileFormat,
                      setup_.config->PathsRef().rules_file.c_str());

  const std::shared_ptr<RuleSet> rules = setup_.host->rules_snapshot();

  if (ImGui::Button(strings::kRulesReload)) {
    const bool ok = setup_.host->ReloadRules();
    const std::shared_ptr<RuleSet> reloaded = setup_.host->rules_snapshot();
    if (ok && reloaded != nullptr) {
      rules_status_ =
          Fmt(strings::kRulesReloadOkFormat, static_cast<int>(reloaded->size()));
    } else {
      rules_status_ = strings::kRulesReloadFailed;
    }
    selected_rule_ = -1;
  }
  if (!rules_status_.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", rules_status_.c_str());
  }

  if (setup_.host->safe_mode()) {
    ImGui::TextColored(kColorWarn, "%s", strings::kRulesSafeModeHint);
  }

  if (rules == nullptr || rules->empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("%s", strings::kRulesNone);
    if (rules != nullptr && rules->skipped() > 0) {
      ImGui::TextColored(kColorWarn, strings::kRulesSkippedFormat,
                         static_cast<int>(rules->skipped()));
    }
    return;
  }

  ImGui::Separator();
  ImGui::TextDisabled(strings::kRulesCountFormat,
                      static_cast<int>(rules->size()));
  if (rules->skipped() > 0) {
    ImGui::SameLine();
    ImGui::TextColored(kColorWarn, strings::kRulesSkippedFormat,
                       static_cast<int>(rules->skipped()));
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  const ImVec2 size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f);

  if (ImGui::BeginTable("##rules", 5, flags, size)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(strings::kColRuleEnabled,
                            ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColRuleIndex, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColName, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(strings::kColRuleHits, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColState, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < rules->size(); ++i) {
      const Rule& rule = rules->rules()[i];
      const bool enabled = rules->enabled(i);
      const RuleCounters counters = rules->counters(i);

      ImGui::PushID(rule.id.c_str());
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      bool value = enabled;
      const bool can_toggle = !setup_.host->safe_mode();
      if (!can_toggle) ImGui::BeginDisabled();
      if (ImGui::Checkbox("##enabled", &value) && can_toggle) {
        setup_.host->SetRuleEnabled(rule.id, value);
      }
      if (!can_toggle) ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(1);
      ImGui::TextDisabled("%d", static_cast<int>(i + 1));

      ImGui::TableSetColumnIndex(2);
      const bool selected = selected_rule_ == static_cast<int>(i);
      if (ImGui::Selectable(rule.name.c_str(), selected,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        selected_rule_ = static_cast<int>(i);
      }

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%lld / %lld", static_cast<long long>(counters.hits),
                  static_cast<long long>(counters.blocked));

      ImGui::TableSetColumnIndex(4);
      ImGui::TextColored(enabled ? kColorOk : kColorDim, "%s",
                         enabled ? strings::kRuleStatusEnabled
                                 : strings::kRuleStatusDisabled);

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (selected_rule_ >= 0 &&
      selected_rule_ < static_cast<int>(rules->size())) {
    const Rule& rule = rules->rules()[static_cast<std::size_t>(selected_rule_)];
    ImGui::Separator();
    ImGui::TextUnformatted(rule.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(strings::kRuleIdFormat, rule.id.c_str());

    ImGui::TextDisabled("%s · %s",
                        rule.type == RuleType::AutoSummary
                            ? strings::kRuleTypeAutoSummary
                            : strings::kRuleTypeMatch,
                        rule.delivery_phase ? strings::kRulePhaseDelivery
                                            : strings::kRulePhaseSource);

    if (rule.target.source.empty() && rule.target.notifier.empty()) {
      ImGui::TextDisabled("%s", strings::kRuleTargetGlobal);
    } else {
      if (!rule.target.source.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < rule.target.source.size(); ++i) {
          if (i > 0) joined += "  ";
          joined += rule.target.source[i];
        }
        ImGui::TextWrapped(strings::kRuleTargetSourceFormat, joined.c_str());
      }
      if (!rule.target.notifier.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < rule.target.notifier.size(); ++i) {
          if (i > 0) joined += "  ";
          joined += rule.target.notifier[i];
        }
        ImGui::TextWrapped(strings::kRuleTargetNotifierFormat, joined.c_str());
      }
    }

    if (rule.type == RuleType::AutoSummary) {
      ImGui::TextDisabled(strings::kRuleFromFormat, rule.auto_from.c_str());
      ImGui::TextDisabled(strings::kRuleMaxFormat, rule.auto_max_codepoints);
      if (rule.auto_only_if_empty) {
        ImGui::TextDisabled("%s", strings::kRuleOnlyIfEmpty);
      }
    } else {
      ImGui::TextWrapped("%s", rule.match_text.empty()
                                   ? strings::kRuleNoMatch
                                   : rule.match_text.c_str());
      if (rule.case_insensitive) {
        ImGui::TextDisabled("%s", strings::kRuleCaseInsensitive);
      }
      if (rule.drop) ImGui::TextColored(kColorBad, "%s", strings::kRuleDrop);
      if (rule.handle) ImGui::TextDisabled("%s", strings::kRuleHandle);
      if (rule.has_rewrite()) {
        if (rule.has_title) {
          ImGui::TextWrapped(strings::kRuleRewriteTitleFormat,
                             rule.title_template.c_str());
        }
        if (rule.has_content) {
          ImGui::TextWrapped(strings::kRuleRewriteContentFormat,
                             rule.content_template.c_str());
        }
        if (rule.has_summary) {
          ImGui::TextWrapped(strings::kRuleRewriteSummaryFormat,
                             rule.summary_template.c_str());
        }
        if (rule.has_level) {
          ImGui::TextWrapped(strings::kRuleRewriteLevelFormat,
                             strings::LevelText(rule.level));
        }
        if (rule.has_importance) {
          ImGui::TextWrapped(strings::kRuleRewriteImportanceFormat,
                             static_cast<int>(rule.importance));
        }
        if (rule.has_time) {
          ImGui::TextWrapped(strings::kRuleRewriteTimeFormat,
                             rule.time_template.c_str());
        }
      } else {
        ImGui::TextDisabled("%s", strings::kRuleNoRewrite);
      }

      ImGui::TextDisabled("%s", rule.save.enabled ? strings::kRuleSaveEnabled
                                                  : strings::kRuleSaveDisabled);
      if (rule.save.enabled) {
        if (rule.save.path.empty()) {
          ImGui::TextWrapped("%s", strings::kRuleSaveDefaultPath);
        } else {
          ImGui::TextWrapped(strings::kRuleSavePathFormat, rule.save.path.c_str());
        }
        if (setup_.config == nullptr || !setup_.config->App().allow_rule_save) {
          ImGui::TextColored(kColorWarn, "%s", strings::kRuleSaveGlobalOff);
        }
      }
    }
  }
}

void UiPanel::DrawRoutingPage() {
  if (setup_.router == nullptr || setup_.config == nullptr) return;

  ImGui::SeparatorText(strings::kRoutingTable1);
  const std::vector<Subscriber>& subscribers = setup_.router->subscribers();
  if (subscribers.empty()) {
    ImGui::TextDisabled("%s", strings::kRoutingNoNotifier);
  } else if (ImGui::BeginTable("##routing1", 3,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(strings::kColNotifier, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(strings::kColSource, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kColPatterns, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const Subscriber& subscriber : subscribers) {
      ImGui::PushID(subscriber.id.c_str());
      ImGui::TableNextRow();
      const bool from_config =
          setup_.config->Data().routing.find(subscriber.id) != setup_.config->Data().routing.end();

      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(subscriber.id.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(from_config ? strings::kRoutingFromConfig : strings::kRoutingFromMeta);
      ImGui::TableSetColumnIndex(2);
      if (subscriber.all_topics) {
        ImGui::TextUnformatted(strings::kRoutingAllTopics);
      } else if (subscriber.patterns.empty()) {
        ImGui::TextUnformatted(strings::kRoutingNoneTopics);
      } else {
        std::string joined;
        for (std::size_t i = 0; i < subscriber.patterns.size(); ++i) {
          if (i > 0) joined += "  ";
          joined += subscriber.patterns[i];
        }
        ImGui::TextUnformatted(joined.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText(strings::kRoutingTable2);
  std::vector<std::string> topics;
  const std::vector<const OwnedNotification*> history = setup_.pipeline->History();
  for (const OwnedNotification* n : history) {
    if (n == nullptr) continue;
    if (std::find(topics.begin(), topics.end(), n->topic) == topics.end()) {
      topics.push_back(n->topic);
    }
  }

  if (topics.empty()) {
    ImGui::TextDisabled("%s", strings::kRoutingNoTopic);
  } else if (ImGui::BeginTable("##routing2", 2,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(strings::kColTopic, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(strings::kColReceivers, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const std::string& topic : topics) {
      ImGui::PushID(topic.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(topic.c_str());
      ImGui::TableSetColumnIndex(1);
      std::vector<std::string> receivers;
      setup_.router->Match(topic, &receivers);
      if (receivers.empty()) {
        ImGui::TextColored(kColorBad, "%s", strings::kRoutingNoSubscribers);
      } else {
        std::string joined;
        for (std::size_t i = 0; i < receivers.size(); ++i) {
          if (i > 0) joined += "  ";
          joined += receivers[i];
        }
        ImGui::TextUnformatted(joined.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (setup_.pipeline != nullptr) {
    const NotificationPipeline::Stats stats = setup_.pipeline->SnapshotStats();
    if (stats.undelivered > 0) {
      ImGui::TextDisabled(strings::kRoutingUnroutedFormat,
                          static_cast<long long>(stats.undelivered));
    }
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", setup_.config->PathsRef().config_file.c_str());
}

void UiPanel::DrawLogPage() {
  if (state_ == nullptr) return;
  OnLogSnapshot();
  LogFilter& filter = state_->log;

  ImGui::TextUnformatted(strings::kFilterLevel);
  ImGui::SameLine();
  for (int i = 0; i < kLogLevelCount; ++i) {
    bool on = filter.levels.Has(kLogLevels[i]);
    ImGui::PushID(i);
    if (ImGui::Checkbox(strings::LogLevelText(kLogLevels[i]), &on)) filter.levels.Set(i, on);
    ImGui::PopID();
    if (i + 1 < kLogLevelCount) ImGui::SameLine();
  }

  AddUnique(&log_sources_, "host");
  for (const log::Entry& entry : log_entries_) AddUnique(&log_sources_, entry.source);

  ImGui::SetNextItemWidth(160.0f);
  const char* const source_label =
      filter.source.empty() ? strings::kFilterSourceAll : filter.source.c_str();
  if (ImGui::BeginCombo(strings::kFilterSource, source_label)) {
    if (ImGui::Selectable(strings::kFilterSourceAll, filter.source.empty())) filter.source.clear();
    for (const std::string& source : log_sources_) {
      if (ImGui::Selectable(source.c_str(), filter.source == source)) filter.source = source;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(strings::kLogSearch);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200.0f);
  InputTextStdString("##log_search", "log_text", &filter.text, 128, &log_text_state_);

  ImGui::Checkbox(strings::kLogFollow, &filter.follow);
  ImGui::SameLine();
  ImGui::Checkbox(strings::kLogPause, &filter.paused);

  const std::string log_file = (setup_.host != nullptr) ? setup_.host->log_file_path() : "";
  const bool has_file = !log_file.empty();
  if (!has_file) ImGui::BeginDisabled();
  if (ImGui::Button(strings::kLogOpenFile) && setup_.host != nullptr) {
    setup_.host->OpenInShell(log_file);
  }
  ImGui::SameLine();
  if (ImGui::Button(strings::kLogOpenDir) && setup_.host != nullptr) {
    setup_.host->OpenInShell(util::path_dir(log_file));
  }
  if (!has_file) ImGui::EndDisabled();
  if (!has_file) ImGui::TextDisabled("%s", strings::kLogNoFile);

  if (filter.paused) ImGui::TextDisabled("%s", strings::kLogPausedHint);
  if (log_dropped_ > 0) {
    ImGui::TextDisabled(strings::kLogDroppedFormat,
                        static_cast<unsigned long long>(log_dropped_));
  }

  std::vector<const log::Entry*> items;
  items.reserve(log_entries_.size());
  for (const log::Entry& entry : log_entries_) {
    if (MatchesLogFilter(filter, entry)) items.push_back(&entry);
  }

  const float height = ImGui::GetTextLineHeightWithSpacing() * 14.0f;
  if (ImGui::BeginChild("##log", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    if (items.empty()) {
      ImGui::TextDisabled("%s", log_entries_.empty() ? strings::kLogEmpty : strings::kNoMatch);
    } else {
      const std::int64_t now = util::now_unix_ms();
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(items.size()), ImGui::GetTextLineHeightWithSpacing());
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const log::Entry& entry = *items[static_cast<std::size_t>(row)];
          char time_text[32];
          util::format_short_time(entry.time_unix_ms, now, time_text, sizeof(time_text));
          ImGui::PushStyleColor(ImGuiCol_Text,
                                ImGui::ColorConvertU32ToFloat4(LogLevelColor(entry.level)));
          ImGui::Text("%s [%s] %s %s", time_text, strings::LogLevelText(entry.level),
                      entry.source, entry.message);
          ImGui::PopStyleColor();
        }
      }
    }

    if (filter.follow) ImGui::SetScrollY(ImGui::GetScrollMaxY());
    if (filter.follow && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f) {
      filter.follow = false;
    }
  }
  ImGui::EndChild();
}

void UiPanel::DrawAboutPage() {
  if (setup_.host == nullptr) return;

  ImGui::SeparatorText(strings::kSecBuild);
  LabelRow(strings::kBuildVersion, SM_VERSION_STRING);
  LabelRow(strings::kBuildDetail, SM_VERSION_DETAIL);
  LabelRow(strings::kBuildApi, std::to_string(static_cast<int>(SM_API_VERSION)));
  LabelRow(strings::kBuildType, BuildTypeText());
  LabelRow(strings::kBuildCompiler, CompilerText());
  LabelRow(strings::kBuildPlatform, PlatformText());

  ImGui::SeparatorText(strings::kSecPaths);
  const config::Paths& paths = setup_.host->paths();
  PathRow(strings::kPathConfigDir, paths.config_dir, setup_.host);
  PathRow(strings::kPathDataDir, paths.data_dir, setup_.host);
  PathRow(strings::kPathPluginDir, paths.plugin_dir, setup_.host);
  PathRow(strings::kPathLogFile, setup_.host->log_file_path(), setup_.host);
  PathRow(strings::kPathUiState, ui_state_path(), setup_.host);

  ImGui::SeparatorText(strings::kSecInterface);
  if (setup_.config != nullptr && setup_.session != nullptr) {
    int font_size = setup_.config->App().ui_font_size;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt(strings::kFontSize, &font_size, 10, 32, "%d px")) {
      setup_.config->SetUiFontSize(font_size);
      setup_.session->SetFontSize(font_size);
    }
    const std::string font_file = setup_.host->font_file_path();
    if (font_file.empty()) {
      ImGui::TextWrapped(strings::kFontFileFormat, strings::kFontFallbackBuiltin);
    } else {
      ImGui::TextDisabled(strings::kFontFileFormat, font_file.c_str());
    }
  }

  ImGui::SeparatorText(strings::kSecLicense);
  ImGui::TextWrapped("%s", strings::kLicensePending);
  ImGui::TextDisabled("%s", strings::kLicenseHint);

  ImGui::SeparatorText(strings::kSecThirdParty);
  if (ImGui::BeginTable("##third_party", 4, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(strings::kColName, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kBuildVersion, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(strings::kSecLicense, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    struct ThirdParty {
      const char* name;
      const char* version;
      const char* license;
      const char* url;
    };
    static const ThirdParty kDependencies[] = {
        {"Dear ImGui", "1.93.0", "MIT", "https://github.com/ocornut/imgui"},
        {"toml++", "3.4.0", "MIT", "https://github.com/marzer/tomlplusplus"},
        {"GLFW", "3.5.1", "Zlib/libpng", "https://github.com/glfw/glfw"},
#ifdef SM_HAVE_CURL
        // 宿主只用它下载通知图标里的 http(s) 图片（见 image_cache.cpp）；
        // 未启用 libcurl 的构建里网络图标直接判失败。
        {"libcurl", "8.23.1", "curl", "https://curl.se/"},
#endif
    };
    for (const ThirdParty& dependency : kDependencies) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(dependency.name);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(dependency.version);
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(dependency.license);
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(dependency.url);
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText(strings::kSecExit);
  if (ImGui::Button(strings::kExitButton)) setup_.host->RequestExit();
}

}  // namespace host
}  // namespace sm

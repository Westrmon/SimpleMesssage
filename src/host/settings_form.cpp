// SimpleMessage — 由 SettingDef 生成设置表单的实现
#include "host/settings_form.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "imgui.h"

#include "host/strings.h"
#include "platform/dialog.h"
#include "platform/sound.h"

namespace sm {
namespace host {
namespace {

constexpr float kNumberBoxWidth = 140.0f;
constexpr float kMinControlWidth = 120.0f;

// "试听"失败的原因在下一帧的模态框里展示；设置表单同一时刻只有一个实例。
std::string g_preview_error;
bool        g_preview_error_pending = false;

std::string LabelOf(const sm::SettingDef& def) {
  std::string label;
  if (def.label != nullptr && def.label[0] != '\0') {
    label = def.label;
  } else if (def.name != nullptr) {
    label = def.name;
  }
  if (def.requires_restart) label += strings::kSettingRestartSuffix;
  return label;
}

void TooltipFor(const sm::SettingDef& def) {
  if (def.description == nullptr || def.description[0] == '\0') return;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("%s", def.description);
  }
}

const char* FallbackString(const sm::SettingDef& def) {
  return (def.default_string != nullptr) ? def.default_string : "";
}

// 数字框与滑条共用一行：窗口变窄时按比例收缩，避免把滑条挤没。
float NumberBoxWidth(float control_width) {
  const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
  const float max_width = (control_width - spacing) * 0.6f;
  return (kNumberBoxWidth < max_width) ? kNumberBoxWidth : max_width;
}

bool DrawBoolControl(const sm::SettingDef& def, config::PluginSettings& settings) {
  bool value = settings.GetBool(def.name, def.default_bool);
  if (ImGui::Checkbox("##v", &value)) {
    settings.SetBool(def.name, value);
    return true;
  }
  TooltipFor(def);
  return false;
}

bool DrawIntControl(const sm::SettingDef& def, config::PluginSettings& settings,
                    float control_width) {
  std::int64_t value = settings.GetInt(def.name, def.default_int);
  bool changed = false;
  const bool has_slider = (def.max_int > def.min_int);
  const float box_width = NumberBoxWidth(control_width);

  if (has_slider) {
    ImGui::SetNextItemWidth(control_width - box_width - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::SliderScalar("##slider", ImGuiDataType_S64, &value, &def.min_int,
                                   &def.max_int);
    TooltipFor(def);
    ImGui::SameLine();
  }

  ImGui::SetNextItemWidth(has_slider ? box_width : control_width);
  const std::int64_t step = 1;
  changed |= ImGui::InputScalar("##box", ImGuiDataType_S64, &value, &step, nullptr, "%lld");

  if (value < def.min_int) value = def.min_int;
  if (value > def.max_int) value = def.max_int;

  if (changed) settings.SetInt(def.name, value);
  TooltipFor(def);
  return changed;
}

bool DrawDoubleControl(const sm::SettingDef& def, config::PluginSettings& settings,
                       float control_width) {
  double value = settings.GetDouble(def.name, def.default_double);
  bool changed = false;
  const bool has_slider = (def.max_double > def.min_double);
  const float box_width = NumberBoxWidth(control_width);

  if (has_slider) {
    ImGui::SetNextItemWidth(control_width - box_width - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::SliderScalar("##slider", ImGuiDataType_Double, &value, &def.min_double,
                                   &def.max_double, "%.4f");
    TooltipFor(def);
    ImGui::SameLine();
  }

  ImGui::SetNextItemWidth(has_slider ? box_width : control_width);
  const double step = 0.1;
  changed |= ImGui::InputScalar("##box", ImGuiDataType_Double, &value, &step, nullptr, "%.4f");

  if (value < def.min_double) value = def.min_double;
  if (value > def.max_double) value = def.max_double;

  if (changed) settings.SetDouble(def.name, value);
  TooltipFor(def);
  return changed;
}

bool DrawStringControl(const sm::SettingDef& def, config::PluginSettings& settings,
                       TextInputState* state, float control_width) {
  const std::size_t capacity = (def.max_string_bytes != 0) ? def.max_string_bytes
                                                           : sm::limits::kMaxStringSettingBytes;
  std::string value = settings.GetStringCopy(def.name, FallbackString(def));

  ImGui::SetNextItemWidth(control_width);
  const bool changed = InputTextStdString("##v", def.name, &value, capacity, state);
  TooltipFor(def);

  if (changed) {
    settings.SetString(def.name, value);
    return true;
  }
  return false;
}

// 只有绝对路径能从中推出有用的初始目录；相对路径按配置目录解析。
std::string InitialDirectoryFor(const std::string& value, const std::string& base_dir) {
  const std::size_t slash = value.find_last_of("/\\");
  if (slash == std::string::npos || slash == 0) return base_dir;
  const bool absolute = (slash >= 1 && value[1] == ':') ||
                        value[0] == '\\' || value[0] == '/';
  return absolute ? value.substr(0, slash) : base_dir;
}

bool DrawSoundControl(const sm::SettingDef& def, config::PluginSettings& settings,
                      TextInputState* state, float control_width,
                      const std::string& base_dir) {
  const std::size_t capacity = (def.max_string_bytes != 0)
                                   ? def.max_string_bytes
                                   : sm::limits::kMaxStringSettingBytes;
  std::string value = settings.GetStringCopy(def.name, FallbackString(def));

  const ImGuiStyle& style = ImGui::GetStyle();
  const float browse_width =
      ImGui::CalcTextSize(strings::kSettingBrowse).x + style.FramePadding.x * 2.0f;
  const float preview_width =
      ImGui::CalcTextSize(strings::kSettingPreview).x + style.FramePadding.x * 2.0f;
  const float input_width =
      control_width - browse_width - preview_width - style.ItemSpacing.x * 2.0f;

  ImGui::SetNextItemWidth(input_width > kMinControlWidth ? input_width
                                                         : kMinControlWidth);
  bool changed = InputTextStdString("##v", def.name, &value, capacity, state);
  TooltipFor(def);

  ImGui::SameLine();
  if (ImGui::Button(strings::kSettingBrowse)) {
    platform::OpenFileDialogOptions options;
    options.initial_directory = InitialDirectoryFor(value, base_dir);
    options.filter_label = strings::kSettingSoundFilter;
    options.filter_extension = "wav;mp3";
    options.all_files_label = strings::kSettingAllFilesFilter;
    const std::string picked = platform::ShowOpenFileDialog(options);
    if (!picked.empty()) {
      value = picked;
      changed = true;
    }
  }

  ImGui::SameLine();
  if (ImGui::Button(strings::kSettingPreview)) {
    std::string error;
    if (!platform::PreviewSound(value, base_dir, &error)) {
      g_preview_error = std::move(error);
      g_preview_error_pending = true;
    }
  }

  if (changed) settings.SetString(def.name, value);
  return changed;
}

bool DrawEnumControl(const sm::SettingDef& def, config::PluginSettings& settings,
                     float control_width) {
  // 必须拷贝：下面的 SetString() 会改写设置值，指向内部的指针会失效。
  const std::string current = settings.GetStringCopy(def.name, FallbackString(def));
  std::string preview = current;
  for (std::size_t i = 0; i < def.options.size; ++i) {
    const sm::SettingOption& option = def.options.data[i];
    if (option.value != nullptr && current == option.value) {
      preview = (option.label != nullptr && option.label[0] != '\0') ? option.label
                                                                    : option.value;
      break;
    }
  }

  ImGui::SetNextItemWidth(control_width);
  bool changed = false;
  if (ImGui::BeginCombo("##v", preview.c_str())) {
    for (std::size_t i = 0; i < def.options.size; ++i) {
      const sm::SettingOption& option = def.options.data[i];
      if (option.value == nullptr) continue;
      const bool selected = current == option.value;
      const char* const text =
          (option.label != nullptr && option.label[0] != '\0') ? option.label : option.value;
      if (ImGui::Selectable(text, selected)) {
        settings.SetString(def.name, option.value);
        changed = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  } else {
    TooltipFor(def);
  }
  return changed;
}

float LabelColumnWidth(sm::Span<const sm::SettingDef> schema) {
  float width = 0.0f;
  for (std::size_t i = 0; i < schema.size; ++i) {
    const std::string label = LabelOf(schema.data[i]);
    const float text_width = ImGui::CalcTextSize(label.c_str()).x;
    if (text_width > width) width = text_width;
  }
  return width + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
}

bool DrawControl(const sm::SettingDef& def, config::PluginSettings& settings,
                 TextInputState* state, float control_width,
                 const std::string& base_dir) {
  switch (def.type) {
    case sm::SettingType::Bool:   return DrawBoolControl(def, settings);
    case sm::SettingType::Int:    return DrawIntControl(def, settings, control_width);
    case sm::SettingType::Double: return DrawDoubleControl(def, settings, control_width);
    case sm::SettingType::String: return DrawStringControl(def, settings, state, control_width);
    case sm::SettingType::Enum:   return DrawEnumControl(def, settings, control_width);
    case sm::SettingType::Sound:
      return DrawSoundControl(def, settings, state, control_width, base_dir);
  }
  return false;
}

}  // namespace

bool DrawSettingsForm(const char* id_scope, config::PluginSettings& settings,
                      sm::Span<const sm::SettingDef> schema, TextInputState* state,
                      const char* base_dir) {
  if (schema.data == nullptr || schema.size == 0) return false;

  const std::string base = (base_dir != nullptr) ? base_dir : "";

  ImGui::PushID(id_scope != nullptr ? id_scope : "");
  const float label_width = LabelColumnWidth(schema);
  bool any_changed = false;

  for (std::size_t i = 0; i < schema.size; ++i) {
    const sm::SettingDef& def = schema.data[i];
    if (def.name == nullptr || def.name[0] == '\0') continue;

    const std::string label = LabelOf(def);
    const float remaining = ImGui::GetContentRegionAvail().x - label_width;
    const float control_width = remaining > kMinControlWidth ? remaining : kMinControlWidth;

    ImGui::PushID(def.name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    TooltipFor(def);
    ImGui::SameLine(label_width);

    any_changed |= DrawControl(def, settings, state, control_width, base);
    ImGui::PopID();
  }

  ImGui::PopID();

  if (g_preview_error_pending) {
    ImGui::OpenPopup(strings::kSettingPreviewFailedTitle);
    g_preview_error_pending = false;
  }
  const ImGuiViewport* const viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(420.0f, 0.0f),
      ImVec2(viewport->WorkSize.x * 0.85f, viewport->WorkSize.y * 0.85f));
  if (ImGui::BeginPopupModal(strings::kSettingPreviewFailedTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(g_preview_error.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button(strings::kConfirm)) {
      g_preview_error.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  return any_changed;
}

}  // namespace host
}  // namespace sm

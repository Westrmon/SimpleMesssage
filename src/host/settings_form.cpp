// SimpleMessage — 由 SettingDef 生成设置表单的实现
#include "host/settings_form.h"

#include <cstdint>
#include <cstring>

#include "imgui.h"

#include "host/strings.h"

namespace sm {
namespace host {
namespace {

constexpr float kNumberBoxWidth = 90.0f;
constexpr float kMinControlWidth = 120.0f;

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

  if (def.max_int > def.min_int) {
    ImGui::SetNextItemWidth(control_width - kNumberBoxWidth - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::SliderScalar("##slider", ImGuiDataType_S64, &value, &def.min_int,
                                   &def.max_int);
    TooltipFor(def);
    ImGui::SameLine();
  }

  ImGui::SetNextItemWidth(def.max_int > def.min_int ? kNumberBoxWidth : control_width);
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

  if (def.max_double > def.min_double) {
    ImGui::SetNextItemWidth(control_width - kNumberBoxWidth - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::SliderScalar("##slider", ImGuiDataType_Double, &value, &def.min_double,
                                   &def.max_double, "%.4f");
    TooltipFor(def);
    ImGui::SameLine();
  }

  ImGui::SetNextItemWidth(def.max_double > def.min_double ? kNumberBoxWidth : control_width);
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
  std::string value = settings.GetString(def.name, FallbackString(def));

  ImGui::SetNextItemWidth(control_width);
  const bool changed = InputTextStdString("##v", def.name, &value, capacity, state);
  TooltipFor(def);

  if (changed) {
    settings.SetString(def.name, value);
    return true;
  }
  return false;
}

bool DrawEnumControl(const sm::SettingDef& def, config::PluginSettings& settings,
                     float control_width) {
  const char* const current = settings.GetString(def.name, FallbackString(def));
  std::string preview = current;
  for (std::size_t i = 0; i < def.options.size; ++i) {
    const sm::SettingOption& option = def.options.data[i];
    if (option.value != nullptr && std::strcmp(option.value, current) == 0) {
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
      const bool selected = std::strcmp(option.value, current) == 0;
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
                 TextInputState* state, float control_width) {
  switch (def.type) {
    case sm::SettingType::Bool:   return DrawBoolControl(def, settings);
    case sm::SettingType::Int:    return DrawIntControl(def, settings, control_width);
    case sm::SettingType::Double: return DrawDoubleControl(def, settings, control_width);
    case sm::SettingType::String: return DrawStringControl(def, settings, state, control_width);
    case sm::SettingType::Enum:   return DrawEnumControl(def, settings, control_width);
  }
  return false;
}

}  // namespace

bool DrawSettingsForm(const char* id_scope, config::PluginSettings& settings,
                      sm::Span<const sm::SettingDef> schema, TextInputState* state) {
  if (schema.data == nullptr || schema.size == 0) return false;

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

    any_changed |= DrawControl(def, settings, state, control_width);
    ImGui::PopID();
  }

  ImGui::PopID();
  return any_changed;
}

}  // namespace host
}  // namespace sm

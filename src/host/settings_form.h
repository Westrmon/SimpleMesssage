// SimpleMessage — 由 SettingDef 生成设置表单
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sm/plugin.h"
#include "sm/types.h"

#include "host/config.h"
#include "host/text_input.h"

namespace sm {
namespace host {

bool DrawSettingsForm(const char* id_scope, config::PluginSettings& settings,
                      sm::Span<const sm::SettingDef> schema, TextInputState* text_state);

}  // namespace host
}  // namespace sm

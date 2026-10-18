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

// base_dir 用于解析 Sound 设置里的相对路径（与插件运行时的 config_dir 一致），
// 也作为"浏览…"对话框的默认目录；可为 nullptr。
bool DrawSettingsForm(const char* id_scope, config::PluginSettings& settings,
                      sm::Span<const sm::SettingDef> schema, TextInputState* text_state,
                      const char* base_dir = nullptr);

}  // namespace host
}  // namespace sm

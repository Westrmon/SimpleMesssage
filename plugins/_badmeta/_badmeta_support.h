// SimpleMessage 测试插件 —— 元信息坏插件的共用零件

#pragma once

#include "sm/export.h"

namespace sm_badmeta {

constexpr sm::PluginMeta BaseMeta(const char* id, const char* description) {
  sm::PluginMeta meta{};
  meta.id = id;
  meta.name = id;
  meta.author = "SimpleMessage 测试";
  meta.version = "1.0.0";
  meta.description = description;
  meta.kind = sm::PluginKind::Source;
  meta.host_range = ">=0.1.0,<1.0.0";
  meta.subscriptions = sm::Span<const char*>{};
  meta.settings = sm::Span<const sm::SettingDef>{};
  meta.api_version = SM_API_VERSION;
  return meta;
}

}  // namespace sm_badmeta

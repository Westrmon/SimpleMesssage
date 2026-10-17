// SimpleMessage SDK — 插件 DLL 导出宏与入口
#pragma once

#include <cstdint>

#include "sm/plugin.h"
#include "sm/types.h"
#include "sm/version.h"

// 导出修饰
#if defined(_WIN32) || defined(_WIN64)
#define SM_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SM_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// 宿主要求的四个导出符号的声明。
#define SM_DECLARE_PLUGIN_EXPORTS()                                        \
  SM_PLUGIN_EXPORT std::int32_t sm_abi_version();                          \
  SM_PLUGIN_EXPORT const sm::PluginMeta* sm_plugin_meta();                 \
  SM_PLUGIN_EXPORT sm::IPlugin* sm_create_plugin(sm::IHostContext* host);  \
  SM_PLUGIN_EXPORT void sm_destroy_plugin(sm::IPlugin* plugin);

#define SM_DEFINE_PLUGIN(MetaSymbol, PluginClass)                             \
  SM_PLUGIN_EXPORT std::int32_t sm_abi_version() {                            \
    return SM_API_VERSION;                                                    \
  }                                                                           \
  SM_PLUGIN_EXPORT const sm::PluginMeta* sm_plugin_meta() {                   \
    return &(MetaSymbol);                                                     \
  }                                                                           \
  SM_PLUGIN_EXPORT sm::IPlugin* sm_create_plugin(sm::IHostContext* host) {    \
    return new PluginClass(host);                                             \
  }                                                                           \
  SM_PLUGIN_EXPORT void sm_destroy_plugin(sm::IPlugin* plugin) {              \
    delete plugin;                                                            \
  }


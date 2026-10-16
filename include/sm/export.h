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

// SM_DEFINE_PLUGIN — 生成全部四个导出符号。
// MetaSymbol 必须是静态存储期的 PluginMeta，PluginClass 必须可被 new PluginClass(host) 构造；
// 必须在全局作用域使用（extern "C" 符号不能在 namespace 里）。
// 用法见 docs/插件开发/02-SDK接口参考.md §8.4。
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


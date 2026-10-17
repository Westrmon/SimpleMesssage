// SimpleMessage — config.toml 的 [[instances]] / [removed_types] 解析与写回
//
// 这些函数只处理 TOML 结构，不接触插件管理器或平台层，因此可以在
// tests/test_instances.cpp 里单独测试。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <toml.hpp>

#include "host/config.h"

namespace sm {
namespace config {

struct InstanceParseResult {
  std::vector<InstanceConfig> instances;
  std::vector<std::string>    removed_types;
  std::vector<std::string>    warnings;
  bool                        fatal = false;
  // 有内容被跳过/纠正，写回时应把整份配置标脏。
  bool                        corrected = false;
};

InstanceParseResult ParseInstances(const toml::table& root);

// 重建 root 里的 instances 与 removed_types；两者都为空时不写出对应键。
void WriteInstances(toml::table* root,
                    const std::vector<InstanceConfig>& instances,
                    const std::vector<std::string>& removed_types);

// v1 -> v2：把 [plugins] 的每个 id = bool 转成一条 canonical 实例，删掉旧表，
// 并写入 schema_version = 2。返回 false 只表示结构完全不可用（[plugins] 不是表）；
// 单条非法项跳过并写进 warnings。
bool MigratePluginsV1(toml::table* root, std::vector<std::string>* warnings);

}  // namespace config
}  // namespace sm

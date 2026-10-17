// SimpleMessage — 插件实例 id 与 config.toml [[instances]] 的单元测试
#include "test_framework.h"

#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <toml.hpp>

#include "host/instances.h"
#include "host/plugin_id.h"

namespace {

using sm::config::InstanceConfig;
using sm::config::InstanceParseResult;
using sm::config::ParseInstances;
using sm::config::WriteInstances;

toml::table Parse(const std::string& text) { return toml::parse(text); }

const InstanceConfig* Find(const InstanceParseResult& parsed, const std::string& id) {
  for (const InstanceConfig& instance : parsed.instances) {
    if (instance.id == id) return &instance;
  }
  return nullptr;
}

int WarningCount(const InstanceParseResult& parsed, const std::string& needle) {
  int count = 0;
  for (const std::string& warning : parsed.warnings) {
    if (warning.find(needle) != std::string::npos) ++count;
  }
  return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// 实例 id
// ---------------------------------------------------------------------------

SM_TEST(instance_id, valid_type_ids) {
  SM_CHECK(sm::host::IsValidPluginTypeId("source.email"));
  SM_CHECK(sm::host::IsValidPluginTypeId("notifier.win_toast"));
  SM_CHECK(sm::host::IsValidPluginTypeId("a-1_2.3"));

  SM_CHECK(!sm::host::IsValidPluginTypeId(""));
  SM_CHECK(!sm::host::IsValidPluginTypeId("Source.Email"));
  SM_CHECK(!sm::host::IsValidPluginTypeId("source.email@2"));
  SM_CHECK(!sm::host::IsValidPluginTypeId("sm.host"));
  SM_CHECK(!sm::host::IsValidPluginTypeId(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

SM_TEST(instance_id, canonical_and_numbered) {
  SM_CHECK(sm::host::IsValidInstanceId("source.email"));
  SM_CHECK(sm::host::IsValidInstanceId("source.email@2"));
  SM_CHECK(sm::host::IsValidInstanceId("source.email@16"));

  SM_CHECK(!sm::host::IsValidInstanceId("source.email@1"));
  SM_CHECK(!sm::host::IsValidInstanceId("source.email@0"));
  SM_CHECK(!sm::host::IsValidInstanceId("source.email@02"));
  SM_CHECK(!sm::host::IsValidInstanceId("source.email@17"));
  SM_CHECK(!sm::host::IsValidInstanceId("source.email@"));
  SM_CHECK(!sm::host::IsValidInstanceId("@2"));
}

SM_TEST(instance_id, base_and_membership) {
  SM_CHECK_TEXT(sm::host::PluginBaseId("source.email@3"), "source.email");
  SM_CHECK_TEXT(sm::host::PluginBaseId("source.email"), "source.email");
  SM_CHECK_TEXT(sm::host::PluginBaseId("bad@x"), "bad@x");

  SM_CHECK(sm::host::InstanceBelongsToType("source.email", "source.email"));
  SM_CHECK(sm::host::InstanceBelongsToType("source.email@2", "source.email"));
  SM_CHECK(!sm::host::InstanceBelongsToType("source.email@2", "source.emai"));
  SM_CHECK(!sm::host::InstanceBelongsToType("source.emailx@2", "source.email"));
  SM_CHECK(!sm::host::InstanceBelongsToType("other@2", "source.email"));

  SM_CHECK_INT(sm::host::InstanceOrdinal("source.email", "source.email"), 1);
  SM_CHECK_INT(sm::host::InstanceOrdinal("source.email@2", "source.email"), 2);
  SM_CHECK_INT(sm::host::InstanceOrdinal("source.email@9", "source.email"), 9);
  SM_CHECK_INT(sm::host::InstanceOrdinal("source.email", "source.other"), 0);
}

SM_TEST(instance_id, next_id_reuses_lowest_free_ordinal) {
  std::set<std::string> used;
  SM_CHECK_TEXT(sm::host::NextInstanceId("source.email", used), "source.email");

  used.insert("source.email");
  SM_CHECK_TEXT(sm::host::NextInstanceId("source.email", used), "source.email@2");

  used.insert("source.email@2");
  used.insert("source.email@3");
  SM_CHECK_TEXT(sm::host::NextInstanceId("source.email", used), "source.email@4");

  used.erase("source.email@2");
  SM_CHECK_TEXT(sm::host::NextInstanceId("source.email", used), "source.email@2");

  for (int ordinal = 2; ordinal <= 16; ++ordinal) {
    used.insert(sm::host::MakeInstanceId("source.email", ordinal));
  }
  SM_CHECK_TEXT(sm::host::NextInstanceId("source.email", used), "");
}

SM_TEST(instance_id, make_instance_id) {
  SM_CHECK_TEXT(sm::host::MakeInstanceId("source.email", 1), "source.email");
  SM_CHECK_TEXT(sm::host::MakeInstanceId("source.email", 2), "source.email@2");
}

// ---------------------------------------------------------------------------
// [[instances]] 解析
// ---------------------------------------------------------------------------

SM_TEST(instances_parse, basic) {
  const toml::table root = Parse(R"(
[[instances]]
id = "source.email"
plugin = "source.email"
name = "工作邮箱"
enabled = true

[[instances]]
id = "source.email@2"
plugin = "source.email"
name = ""
enabled = false
)");

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK(!parsed.fatal);
  SM_CHECK(!parsed.corrected);
  SM_CHECK_INT(parsed.instances.size(), 2);

  const InstanceConfig* first = Find(parsed, "source.email");
  SM_CHECK(first != nullptr);
  if (first != nullptr) {
    SM_CHECK_TEXT(first->plugin, "source.email");
    SM_CHECK_TEXT(first->name, "工作邮箱");
    SM_CHECK(first->enabled);
  }

  const InstanceConfig* second = Find(parsed, "source.email@2");
  SM_CHECK(second != nullptr);
  if (second != nullptr) {
    SM_CHECK_TEXT(second->plugin, "source.email");
    SM_CHECK(!second->enabled);
  }
}

SM_TEST(instances_parse, infers_plugin_and_defaults) {
  const toml::table root = Parse(R"(
[[instances]]
id = "source.email@2"
)");

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.instances.size(), 1);
  SM_CHECK(parsed.corrected);
  SM_CHECK_INT(WarningCount(parsed, "缺少 plugin"), 1);
  if (!parsed.instances.empty()) {
    SM_CHECK_TEXT(parsed.instances[0].plugin, "source.email");
    SM_CHECK_TEXT(parsed.instances[0].name, "");
    SM_CHECK(parsed.instances[0].enabled);
  }
}

SM_TEST(instances_parse, rejects_invalid_and_duplicate_ids) {
  const toml::table root = Parse(R"(
[[instances]]
id = "source.email@1"
plugin = "source.email"

[[instances]]
id = "source.email"
plugin = "source.email"

[[instances]]
id = "source.email"
plugin = "source.email"
)");

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.instances.size(), 1);
  SM_CHECK(parsed.corrected);
  SM_CHECK_INT(WarningCount(parsed, "非法"), 1);
  SM_CHECK_INT(WarningCount(parsed, "重复"), 1);
}

SM_TEST(instances_parse, rejects_mismatched_plugin_and_bad_enabled) {
  const toml::table root = Parse(R"(
[[instances]]
id = "source.email@2"
plugin = "source.webhook"

[[instances]]
id = "source.email"
plugin = "source.email"
enabled = "yes"
unknown_key = 1
)");

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.instances.size(), 1);
  SM_CHECK(parsed.corrected);
  SM_CHECK_INT(WarningCount(parsed, "不属于类型"), 1);
  SM_CHECK_INT(WarningCount(parsed, "enabled 不是布尔值"), 1);
  SM_CHECK_INT(WarningCount(parsed, "未知键"), 1);
  if (!parsed.instances.empty()) SM_CHECK(parsed.instances[0].enabled);
}

SM_TEST(instances_parse, non_array_is_fatal) {
  const toml::table root = Parse("instances = 3\n");
  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK(parsed.fatal);
  SM_CHECK(parsed.instances.empty());
}

SM_TEST(instances_parse, removed_types) {
  const toml::table root = Parse(R"(
[removed_types]
"source.email" = true
"source.webhook" = false
"bad id" = true
)");

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.removed_types.size(), 1);
  if (!parsed.removed_types.empty()) {
    SM_CHECK_TEXT(parsed.removed_types[0], "source.email");
  }
  SM_CHECK_INT(WarningCount(parsed, "不是合法的类型 id"), 1);
}

// ---------------------------------------------------------------------------
// 写回
// ---------------------------------------------------------------------------

SM_TEST(instances_write, round_trip) {
  toml::table root;
  std::vector<InstanceConfig> instances;
  instances.push_back({"source.email", "source.email", "工作邮箱", true});
  instances.push_back({"source.email@2", "source.email", "", false});
  std::vector<std::string> removed{"source.webhook"};

  WriteInstances(&root, instances, removed);

  std::ostringstream stream;
  stream << root;
  const toml::table reparsed = Parse(stream.str());
  const InstanceParseResult parsed = ParseInstances(reparsed);

  SM_CHECK_INT(parsed.instances.size(), 2);
  SM_CHECK_INT(parsed.removed_types.size(), 1);
  SM_CHECK(!parsed.corrected);
  if (parsed.instances.size() == 2) {
    SM_CHECK_TEXT(parsed.instances[0].name, "工作邮箱");
    SM_CHECK(parsed.instances[0].enabled);
    SM_CHECK(!parsed.instances[1].enabled);
  }
}

SM_TEST(instances_write, empty_removes_keys) {
  toml::table root = Parse(R"(
[[instances]]
id = "source.email"
plugin = "source.email"

[removed_types]
"source.webhook" = true
)");

  WriteInstances(&root, {}, {});
  SM_CHECK(root.get("instances") == nullptr);
  SM_CHECK(root.get("removed_types") == nullptr);
}

// ---------------------------------------------------------------------------
// v1 -> v2 迁移
// ---------------------------------------------------------------------------

SM_TEST(instances_migrate, plugins_table_becomes_instances) {
  toml::table root = Parse(R"(
schema_version = 1
unknown_top = "keep"

[plugins]
"source.email" = true
"source.webhook" = false

[rules]
"rule.one" = false
)");

  std::vector<std::string> warnings;
  SM_CHECK(sm::config::MigratePluginsV1(&root, &warnings));
  SM_CHECK(warnings.empty());
  SM_CHECK(root.get("plugins") == nullptr);

  const auto* version = root.get("schema_version");
  SM_CHECK(version != nullptr);
  if (version != nullptr) {
    SM_CHECK_INT(version->as_integer()->get(), 2);
  }

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.instances.size(), 2);
  SM_CHECK(!parsed.corrected);
  const InstanceConfig* email = Find(parsed, "source.email");
  const InstanceConfig* webhook = Find(parsed, "source.webhook");
  SM_CHECK(email != nullptr && email->enabled);
  SM_CHECK(webhook != nullptr && !webhook->enabled);

  SM_CHECK(root.get("unknown_top") != nullptr);
  SM_CHECK(root.get("rules") != nullptr);
}

SM_TEST(instances_migrate, invalid_plugin_keys_are_skipped) {
  toml::table root = Parse(R"(
[plugins]
"Source.Email" = true
"source.email" = true
)");

  std::vector<std::string> warnings;
  SM_CHECK(sm::config::MigratePluginsV1(&root, &warnings));
  SM_CHECK_INT(warnings.size(), 1);

  const InstanceParseResult parsed = ParseInstances(root);
  SM_CHECK_INT(parsed.instances.size(), 1);
  if (!parsed.instances.empty()) {
    SM_CHECK_TEXT(parsed.instances[0].id, "source.email");
  }
}

SM_TEST(instances_migrate, missing_plugins_table_only_bumps_version) {
  toml::table root = Parse("schema_version = 1\n");
  std::vector<std::string> warnings;
  SM_CHECK(sm::config::MigratePluginsV1(&root, &warnings));
  SM_CHECK(warnings.empty());
  SM_CHECK(root.get("schema_version") != nullptr);
  SM_CHECK(root.get("instances") == nullptr);
}

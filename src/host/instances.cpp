// SimpleMessage — [[instances]] / [removed_types] 的解析与写回
#include "host/instances.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include "host/plugin_id.h"

namespace sm {
namespace config {
namespace {

constexpr std::int32_t kInstancesSchemaVersion = 2;

bool IsControlChar(unsigned char c) { return c < 0x20u || c == 0x7Fu; }

void TruncateUtf8(std::string* text, std::size_t max_bytes) {
  if (text->size() <= max_bytes) return;
  std::size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>((*text)[cut]) & 0xC0u) == 0x80u) --cut;
  text->resize(cut);
}

std::string NormalizeName(const std::string& raw, bool* corrected,
                          const std::string& instance_id,
                          std::vector<std::string>* warnings) {
  std::string name = raw;
  bool bad = false;
  for (char c : name) {
    if (IsControlChar(static_cast<unsigned char>(c))) {
      bad = true;
      break;
    }
  }
  if (bad) {
    warnings->push_back("实例 " + instance_id + " 的 name 含有控制字符，已清空");
    name.clear();
    *corrected = true;
    return name;
  }
  if (name.size() > host::kMaxInstanceNameBytes) {
    TruncateUtf8(&name, host::kMaxInstanceNameBytes);
    warnings->push_back("实例 " + instance_id + " 的 name 超过 " +
                        std::to_string(host::kMaxInstanceNameBytes) +
                        " 字节，已截断");
    *corrected = true;
  }
  return name;
}

}  // namespace

InstanceParseResult ParseInstances(const toml::table& root) {
  InstanceParseResult result;

  if (const toml::node* node = root.get("instances")) {
    const toml::array* array = node->as_array();
    if (array == nullptr) {
      result.warnings.push_back("instances 不是数组，本次忽略");
      result.fatal = true;
      result.corrected = true;
    } else {
      std::set<std::string> seen;
      std::map<std::string, std::size_t> per_type;

      for (std::size_t index = 0; index < array->size(); ++index) {
        const toml::node& element = (*array)[index];
        const toml::table* entry = element.as_table();
        if (entry == nullptr) {
          result.warnings.push_back("第 " + std::to_string(index + 1) +
                                    " 条 instances 不是表，已跳过");
          result.corrected = true;
          continue;
        }

        const toml::node* id_node = entry->get("id");
        const auto* id_value = (id_node != nullptr) ? id_node->as_string() : nullptr;
        if (id_value == nullptr) {
          result.warnings.push_back("第 " + std::to_string(index + 1) +
                                    " 条 instances 缺少字符串 id，已跳过");
          result.corrected = true;
          continue;
        }
        const std::string id = id_value->get();

        if (!host::IsValidInstanceId(id)) {
          result.warnings.push_back("实例 id \"" + id +
                                    "\" 非法（只允许 类型id 或 类型id@2..16，"
                                    "字符 a-z 0-9 _ . -），已跳过");
          result.corrected = true;
          continue;
        }
        if (!seen.insert(id).second) {
          result.warnings.push_back("实例 id \"" + id + "\" 重复，后一条已跳过");
          result.corrected = true;
          continue;
        }

        std::string plugin;
        if (const toml::node* plugin_node = entry->get("plugin")) {
          const auto* plugin_value = plugin_node->as_string();
          if (plugin_value == nullptr) {
            result.warnings.push_back("实例 " + id + " 的 plugin 不是字符串，已跳过");
            result.corrected = true;
            continue;
          }
          plugin = plugin_value->get();
        } else {
          plugin = host::PluginBaseId(id);
          result.warnings.push_back("实例 " + id + " 缺少 plugin，按类型 id \"" +
                                    plugin + "\" 处理");
          result.corrected = true;
        }

        if (!host::IsValidPluginTypeId(plugin)) {
          result.warnings.push_back("实例 " + id + " 的 plugin \"" + plugin +
                                    "\" 不是合法的类型 id，已跳过");
          result.corrected = true;
          continue;
        }
        if (!host::InstanceBelongsToType(id, plugin)) {
          result.warnings.push_back("实例 id \"" + id + "\" 不属于类型 \"" + plugin +
                                    "\"，已跳过");
          result.corrected = true;
          continue;
        }

        InstanceConfig instance;
        instance.id = id;
        instance.plugin = plugin;

        if (const toml::node* name_node = entry->get("name")) {
          const auto* name_value = name_node->as_string();
          if (name_value == nullptr) {
            result.warnings.push_back("实例 " + id + " 的 name 不是字符串，按自动名处理");
            result.corrected = true;
          } else {
            instance.name = NormalizeName(name_value->get(), &result.corrected, id,
                                          &result.warnings);
          }
        }

        if (const toml::node* enabled_node = entry->get("enabled")) {
          const auto* enabled_value = enabled_node->as_boolean();
          if (enabled_value == nullptr) {
            result.warnings.push_back("实例 " + id + " 的 enabled 不是布尔值，按启用处理");
            result.corrected = true;
          } else {
            instance.enabled = enabled_value->get();
          }
        }

        for (auto&& key_node : *entry) {
          const std::string key(key_node.first.str());
          if (key != "id" && key != "plugin" && key != "name" && key != "enabled") {
            result.warnings.push_back("实例 " + id + " 里有未知键 " + key +
                                      "，本次忽略（写回时丢弃）");
            result.corrected = true;
          }
        }

        const std::size_t count = ++per_type[plugin];
        if (count > host::kMaxInstancesPerType) {
          result.warnings.push_back("类型 " + plugin + " 的实例超过 " +
                                    std::to_string(host::kMaxInstancesPerType) +
                                    " 个，配置仍按原样加载");
        }

        result.instances.push_back(std::move(instance));
      }

      if (result.instances.size() > host::kMaxInstancesTotal) {
        result.warnings.push_back("实例总数超过 " +
                                  std::to_string(host::kMaxInstancesTotal) +
                                  " 个，配置仍按原样加载");
      }
    }
  }

  if (const toml::node* node = root.get("removed_types")) {
    const toml::table* table = node->as_table();
    if (table == nullptr) {
      result.warnings.push_back("removed_types 不是表，本次忽略");
      result.corrected = true;
    } else {
      for (auto&& entry : *table) {
        const std::string type_id(entry.first.str());
        if (!host::IsValidPluginTypeId(type_id)) {
          result.warnings.push_back("removed_types 里的 \"" + type_id +
                                    "\" 不是合法的类型 id，已忽略");
          result.corrected = true;
          continue;
        }
        const auto* value = entry.second.as_boolean();
        if (value == nullptr) {
          result.warnings.push_back("removed_types." + type_id +
                                    " 不是布尔值，按未删除处理");
          result.corrected = true;
          continue;
        }
        if (value->get()) result.removed_types.push_back(type_id);
      }
    }
  }

  return result;
}

void WriteInstances(toml::table* root,
                    const std::vector<InstanceConfig>& instances,
                    const std::vector<std::string>& removed_types) {
  if (root == nullptr) return;

  root->erase("instances");
  if (!instances.empty()) {
    toml::array array;
    for (const InstanceConfig& instance : instances) {
      toml::table entry;
      entry.insert_or_assign("id", instance.id);
      entry.insert_or_assign("plugin", instance.plugin);
      entry.insert_or_assign("name", instance.name);
      entry.insert_or_assign("enabled", instance.enabled);
      array.push_back(std::move(entry));
    }
    root->insert_or_assign("instances", std::move(array));
  }

  root->erase("removed_types");
  if (!removed_types.empty()) {
    toml::table table;
    for (const std::string& type_id : removed_types) {
      table.insert_or_assign(type_id, true);
    }
    root->insert_or_assign("removed_types", std::move(table));
  }
}

bool MigratePluginsV1(toml::table* root, std::vector<std::string>* warnings) {
  if (root == nullptr) return false;

  const toml::node* node = root->get("plugins");
  if (node == nullptr) {
    root->insert_or_assign("schema_version",
                           static_cast<std::int64_t>(kInstancesSchemaVersion));
    return true;
  }

  const toml::table* plugins = node->as_table();
  if (plugins == nullptr) {
    if (warnings != nullptr) {
      warnings->push_back("[plugins] 不是表，迁移时丢弃该节");
    }
    root->erase("plugins");
    root->insert_or_assign("schema_version",
                           static_cast<std::int64_t>(kInstancesSchemaVersion));
    return false;
  }

  toml::array instances;
  if (const toml::node* existing_node = root->get("instances")) {
    if (const toml::array* existing = existing_node->as_array()) {
      instances = *existing;
    }
  }

  std::set<std::string> seen;
  for (auto&& element : instances) {
    if (const toml::table* entry = element.as_table()) {
      if (const toml::node* id_node = entry->get("id")) {
        if (const auto* id_value = id_node->as_string()) seen.insert(id_value->get());
      }
    }
  }

  for (auto&& entry : *plugins) {
    const std::string id(entry.first.str());
    if (!host::IsValidPluginTypeId(id)) {
      if (warnings != nullptr) {
        warnings->push_back("[plugins] 里的 \"" + id +
                            "\" 不是合法的插件 id，迁移时跳过");
      }
      continue;
    }
    if (!seen.insert(id).second) continue;

    bool enabled = true;
    if (const auto* value = entry.second.as_boolean()) {
      enabled = value->get();
    } else if (warnings != nullptr) {
      warnings->push_back("[plugins] 中 " + id + " 的值不是布尔值，迁移时按启用处理");
    }

    toml::table instance;
    instance.insert_or_assign("id", id);
    instance.insert_or_assign("plugin", id);
    instance.insert_or_assign("name", std::string());
    instance.insert_or_assign("enabled", enabled);
    instances.push_back(std::move(instance));
  }

  root->erase("plugins");
  if (!instances.empty()) {
    root->insert_or_assign("instances", std::move(instances));
  } else {
    root->erase("instances");
  }
  root->insert_or_assign("schema_version",
                         static_cast<std::int64_t>(kInstancesSchemaVersion));
  return true;
}

}  // namespace config
}  // namespace sm

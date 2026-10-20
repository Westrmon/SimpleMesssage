// SimpleMessage — 规则层单元测试
//
// 这些用例锁住 rules.toml 的语义：表达式求值顺序、两趟规则、累积改写、
// handle/drop、捕获组模板、启用覆盖与坏规则跳过。它们不依赖窗口层。
#include "test_framework.h"

#include <map>
#include <memory>
#include <string>

#include "host/rule_time.h"
#include "host/rules.h"
#include "sm/summary.h"

namespace {

using sm::host::RuleDiagnostics;
using sm::host::RuleFields;
using sm::host::RuleSet;

struct Parsed {
  std::shared_ptr<RuleSet> set;
  RuleDiagnostics           diagnostics;
  bool                      failed = false;
};

Parsed ParseRules(const std::string& text,
                  const std::map<std::string, bool>& enabled = {}) {
  Parsed parsed;
  parsed.set = RuleSet::Parse(text, "rules.toml", enabled, &parsed.diagnostics,
                              &parsed.failed);
  return parsed;
}

int WarningCount(const RuleDiagnostics& diagnostics) {
  int count = 0;
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity == sm::host::RuleDiagnostic::Severity::Warn) {
      ++count;
    }
  }
  return count;
}

bool HasMessage(const RuleDiagnostics& diagnostics, const std::string& needle) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.message.find(needle) != std::string::npos) return true;
  }
  return false;
}

bool Apply(RuleSet* set, RuleFields* fields, const std::string& source) {
  RuleDiagnostics warnings;
  return set->ApplySource(fields, source, &warnings);
}

bool Deliver(RuleSet* set, RuleFields* fields, const std::string& source,
             const std::string& notifier) {
  RuleDiagnostics warnings;
  return set->ApplyDelivery(fields, source, notifier, &warnings);
}

}  // namespace

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

SM_TEST(rules_expr, empty_expression_always_matches) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "all"
)");
  SM_CHECK(parsed.set != nullptr);
  SM_CHECK_INT(parsed.set->size(), 1);

  std::string title = "x";
  std::string content;
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "source.any"));
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
}

SM_TEST(rules_expr, and_binds_tighter_than_or) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'title =~ "^a$" or content =~ "^hit$" and topic =~ "^t$"'
)");
  SM_CHECK_INT(parsed.set->size(), 1);

  // 直接看“这次调用后 hits 是否增长”。
  std::int64_t previous = 0;
  const auto run = [&](const std::string& title, const std::string& content,
                       const std::string& topic) {
    RuleFields fields(title, content, std::string(), topic, sm::Level::Info);
    parsed.set->ApplySource(&fields, "s", nullptr);
    const std::int64_t now = parsed.set->counters(0).hits;
    const bool matched = now != previous;
    previous = now;
    return matched;
  };
  SM_CHECK(run("a", "nohit", "x"));      // title
  SM_CHECK(run("c", "hit", "t"));        // content and topic
  SM_CHECK(!run("c", "hit", "x"));       // content and false
  SM_CHECK(!run("c", "nohit", "t"));     // false and topic
}

SM_TEST(rules_expr, not_and_parentheses) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'not (title =~ "^a$") and topic =~ "^t$"'
)");

  std::int64_t previous = 0;
  const auto run = [&](const std::string& title, const std::string& topic) {
    RuleFields fields(title, std::string(), std::string(), topic,
                      sm::Level::Info);
    parsed.set->ApplySource(&fields, "s", nullptr);
    const std::int64_t now = parsed.set->counters(0).hits;
    const bool matched = now != previous;
    previous = now;
    return matched;
  };
  SM_CHECK(run("b", "t"));
  SM_CHECK(!run("a", "t"));
  SM_CHECK(!run("b", "x"));
}

SM_TEST(rules_expr, case_insensitive_applies_to_every_field) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
case_insensitive = true
match = 'content =~ "^LOUD$"'
)");

  std::string title;
  std::string content = "loud";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
}

SM_TEST(rules_expr, regex_searches_substring_unless_anchored) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "substring"
match = 'content =~ "abc"'
[[rules]]
id = "anchored"
match = 'content =~ "^abc$"'
)");

  std::string title;
  std::string content = "xx abc yy";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  Apply(parsed.set.get(), &fields, "s");
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
  SM_CHECK_INT(parsed.set->counters(1).hits, 0);
}

SM_TEST(rules_expr, invalid_expression_skips_rule) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "bad-op"
match = 'title == "x"'
[[rules]]
id = "ok"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->skipped(), 1);
  SM_CHECK(HasMessage(parsed.diagnostics, "bad-op"));
}

SM_TEST(rules_expr, duplicate_positive_match_on_one_field_skips_rule) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "twice"
match = 'title =~ "a" and title =~ "b"'
)");
  SM_CHECK_INT(parsed.set->size(), 0);
  SM_CHECK_INT(parsed.set->skipped(), 1);
}

SM_TEST(rules_expr, negative_match_does_not_provide_captures) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'title !~ "^a$"'
[rules.rewrite]
title = "$title.1"
)");
  SM_CHECK_INT(parsed.set->size(), 0);
  SM_CHECK_INT(parsed.set->skipped(), 1);
  SM_CHECK(HasMessage(parsed.diagnostics, "没有该字段的 =~"));
}

// ---------------------------------------------------------------------------
// 重写
// ---------------------------------------------------------------------------

SM_TEST(rules_rewrite, captures_and_literal_dollar) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'title =~ "^Update (\w+)$" and content =~ "^(\w+)$"'
[rules.rewrite]
title = "第 $title.1 号"
content = "$content.1 / $title.0 / $$5"
)");
  SM_CHECK(parsed.set != nullptr);
  SM_CHECK_INT(parsed.set->size(), 1);

  std::string title = "Update 42";
  std::string content = "abc";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));

  SM_CHECK_TEXT(std::string(fields.title()), "第 42 号");
  SM_CHECK_TEXT(std::string(fields.content()), "abc / Update 42 / $5");
  SM_CHECK(fields.changed());
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
}

SM_TEST(rules_rewrite, split_gotify_summary_into_title_and_summary) {
  // source.gotify 在能映射应用时把应用名放进 title、把 Gotify 消息自己的
  // title 与正文前 30 字节拼进 summary，因此要拆分的是 summary，不是 content。
  Parsed parsed = ParseRules(R"RULES(
schema_version = 1
[[rules]]
id = "maaTitle"
name = "MaaTitle"
match = 'summary =~ "^\[(\[MAA\][^\]]*)\]\s*(.*)$" and content =~ "([\s\S]+)"'
[rules.rewrite]
title = "$summary.1"
summary = "$content.1"
)RULES");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->skipped(), 0);

  std::string title = "MAA";
  std::string content =
      "这是 MAA 外部通知测试信息。如果你看到了这段内容，就说明通知发送成功了！";
  std::string summary = "[[MAA] MAA 外部通知测试]  这是 MAA 外部通知测试";
  std::string topic = "gotify.in";
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "source.gotify"));
  SM_CHECK_TEXT(std::string(fields.title()), "[MAA] MAA 外部通知测试");
  SM_CHECK_TEXT(std::string(fields.summary()), content);
  SM_CHECK_TEXT(std::string(fields.content()), content);
}

SM_TEST(rules_rewrite, level_and_summary) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'topic =~ "^alarm$"'
[rules.rewrite]
level = "critical"
summary = "来自 $topic.0"
)");

  std::string title = "t";
  std::string content = "c";
  std::string summary;
  std::string topic = "alarm";
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK(fields.level() == sm::Level::Critical);
  SM_CHECK_TEXT(std::string(fields.summary()), "来自 alarm");
}

SM_TEST(rules_rewrite, importance_and_time_from_capture) {
  Parsed parsed = ParseRules(R"RULES(
schema_version = 1
[[rules]]
id = "r"
match = 'content =~ "发布时间(?:：|:)\s*(\d{4}/\d{1,2}/\d{1,2}\s+\d{1,2}:\d{2}:\d{2})"'
[rules.rewrite]
importance = 4
time = "$content.1"
)RULES");
  SM_CHECK_INT(parsed.set->size(), 1);

  std::string title = "t";
  std::string content = "发布时间：2026/10/20 12:34:56";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info, 0, 111);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));

  std::int64_t expected = 0;
  SM_CHECK(sm::host::ParseNotificationTime("2026/10/20 12:34:56", &expected));
  SM_CHECK(fields.changed());
  SM_CHECK(fields.has_importance());
  SM_CHECK_INT(fields.importance(), 4);
  SM_CHECK(fields.has_time());
  SM_CHECK_INT(fields.time_unix_ms(), expected);
}

SM_TEST(rules_rewrite, invalid_time_keeps_original_and_warns) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'content =~ "^(.+)$"'
[rules.rewrite]
time = "$content.1"
)");
  SM_CHECK_INT(parsed.set->size(), 1);

  std::string title;
  std::string content = "这里没有时间";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info, 0, 111);
  RuleDiagnostics warnings;
  SM_CHECK(parsed.set->ApplySource(&fields, "s", &warnings));
  SM_CHECK(!fields.has_time());
  SM_CHECK_INT(fields.time_unix_ms(), 111);
  SM_CHECK(HasMessage(warnings, "无法识别为时间"));
}

SM_TEST(rules_save, snapshot_carries_rewritten_importance_and_time) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'title =~ "^(.*)$"'
[rules.rewrite]
importance = 2
time = "2026-10-20 12:34:56"
[rules.save]
enabled = true
)");
  SM_CHECK_INT(parsed.set->size(), 1);

  std::string title = "t";
  std::string content = "body";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info, 0, 111);
  sm::host::RuleSaveHits hits;
  RuleDiagnostics warnings;
  SM_CHECK(parsed.set->ApplySource(&fields, "s", &warnings, &hits));
  SM_CHECK_INT(hits.size(), 1);
  if (hits.size() == 1) {
    std::int64_t expected = 0;
    SM_CHECK(sm::host::ParseNotificationTime("2026-10-20 12:34:56", &expected));
    SM_CHECK_INT(hits[0].importance, 2);
    SM_CHECK_INT(hits[0].time_unix_ms, expected);
  }
}

SM_TEST(rules_rewrite, template_group_out_of_range_skips_rule) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'content =~ "(a)(b)$"'
[rules.rewrite]
content = "$content.3"
)");
  SM_CHECK_INT(parsed.set->size(), 0);
  SM_CHECK_INT(parsed.set->skipped(), 1);
  SM_CHECK(HasMessage(parsed.diagnostics, "超出捕获组数量"));
}

SM_TEST(rules_rewrite, empty_title_rewrite_keeps_original) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'content =~ "a" or title =~ "(b)$"'
[rules.rewrite]
title = "$title.1"
)");

  std::string title = "keep";
  std::string content = "a";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.title()), "keep");
}

SM_TEST(rules_rewrite, scale_rules_is_sequential_and_cumulative) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "one"
match = 'title =~ "^a$"'
[rules.rewrite]
title = "b"
[[rules]]
id = "two"
match = 'title =~ "^b$"'
[rules.rewrite]
content = "seen"
)");
  SM_CHECK_INT(parsed.set->size(), 2);

  std::string title = "a";
  std::string content = "no";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.title()), "b");
  SM_CHECK_TEXT(std::string(fields.content()), "seen");
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
  SM_CHECK_INT(parsed.set->counters(1).hits, 1);
}

SM_TEST(rules_rewrite, handle_stops_later_rules) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "one"
handle = true
match = 'title =~ "^a$"'
[rules.rewrite]
title = "b"
[[rules]]
id = "two"
match = 'title =~ "^b$"'
[rules.rewrite]
content = "seen"
)");

  std::string title = "a";
  std::string content = "no";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.title()), "b");
  SM_CHECK_TEXT(std::string(fields.content()), "no");
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
  SM_CHECK_INT(parsed.set->counters(1).hits, 0);
}

SM_TEST(rules_rewrite, drop_terminates_and_counts_block) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "spam"
drop = true
match = 'content =~ "spam"'
[[rules]]
id = "after"
match = 'content =~ "spam"'
)");

  std::string title;
  std::string content = "buy spam now";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(!Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
  SM_CHECK_INT(parsed.set->counters(0).blocked, 1);
  SM_CHECK_INT(parsed.set->counters(1).hits, 0);
}

// ---------------------------------------------------------------------------
// 目标与两趟
// ---------------------------------------------------------------------------

SM_TEST(rules_target, source_patterns_and_global_rules) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "global"
[rules.rewrite]
summary = "g"
[[rules]]
id = "only-a"
[rules.target]
source = ['^source\.a$']
[rules.rewrite]
summary = "a"
[[rules]]
id = "only-b"
[rules.target]
source = ['^source\.b$']
[rules.rewrite]
summary = "b"
)");
  SM_CHECK_INT(parsed.set->size(), 3);

  const auto summary = [&](const std::string& source, std::int64_t* global_hits,
                           std::int64_t* a_hits) {
    std::string title;
    std::string content;
    std::string value;
    std::string topic;
    RuleFields fields(title, content, value, topic, sm::Level::Info);
    Apply(parsed.set.get(), &fields, source);
    *global_hits = parsed.set->counters(0).hits;
    *a_hits = parsed.set->counters(1).hits;
    return std::string(fields.summary());
  };

  std::int64_t global_hits = 0;
  std::int64_t a_hits = 0;
  SM_CHECK_TEXT(summary("source.a", &global_hits, &a_hits), "a");
  SM_CHECK_INT(global_hits, 1);
  SM_CHECK_INT(a_hits, 1);

  SM_CHECK_TEXT(summary("source.b", &global_hits, &a_hits), "b");
  SM_CHECK_INT(global_hits, 2);  // 累积计数
  SM_CHECK_INT(a_hits, 1);       // 没有增加
}

SM_TEST(rules_target, source_type_id_matches_all_instances) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "all-email"
[rules.target]
source = ['^source\.email$']
[rules.rewrite]
summary = "type"
[[rules]]
id = "second-only"
[rules.target]
source = ['^source\.email@2$']
[rules.rewrite]
title = "second"
)");
  SM_CHECK_INT(parsed.set->size(), 2);

  const auto run = [&](const std::string& source, std::string* title,
                       std::string* summary) {
    std::string content;
    std::string topic;
    RuleFields fields(*title, content, *summary, topic, sm::Level::Info);
    SM_CHECK(Apply(parsed.set.get(), &fields, source));
    *title = std::string(fields.title());
    *summary = std::string(fields.summary());
  };

  // 类型 id 命中全部实例。
  std::string title = "t";
  std::string summary;
  run("source.email", &title, &summary);
  SM_CHECK_TEXT(title, "t");
  SM_CHECK_TEXT(summary, "type");

  // 实例 id 只命中自己；同时仍然吃到类型 id 规则。
  title = "t";
  summary.clear();
  run("source.email@2", &title, &summary);
  SM_CHECK_TEXT(title, "second");
  SM_CHECK_TEXT(summary, "type");

  // 第三个实例只吃类型 id 规则。
  title = "t";
  summary.clear();
  run("source.email@3", &title, &summary);
  SM_CHECK_TEXT(title, "t");
  SM_CHECK_TEXT(summary, "type");

  // 前缀相同但不是该类型实例的来源不受影响。
  title = "t";
  summary.clear();
  run("source.emailx", &title, &summary);
  SM_CHECK_TEXT(title, "t");
  SM_CHECK_TEXT(summary, "");
}

SM_TEST(rules_target, source_fallback_applies_to_delivery_phase) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "all-email"
match = 'content =~ "^spam$"'
[rules.target]
source = ['^source\.email$']
notifier = ['^notifier\.imgui$']
[rules.rewrite]
content = "clean"
)");
  SM_CHECK(parsed.set->has_delivery_rules());

  std::string title;
  std::string summary;
  std::string topic;

  // 来源实例 id 回退到类型 id；Notifier 目标仍按完整 id 精确匹配。
  std::string content = "spam";
  RuleFields second(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Deliver(parsed.set.get(), &second, "source.email@2", "notifier.imgui"));
  SM_CHECK_TEXT(std::string(second.content()), "clean");

  content = "spam";
  RuleFields third(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Deliver(parsed.set.get(), &third, "source.email@3", "notifier.imgui@2"));
  SM_CHECK_TEXT(std::string(third.content()), "spam");
}

SM_TEST(rules_target, notifier_targets_run_only_in_delivery_phase) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "toast"
match = 'content =~ "spam"'
[rules.target]
notifier = ['^notifier\.imgui$']
[rules.rewrite]
content = "clean"
[[rules]]
id = "drop-gotify"
drop = true
match = 'content =~ "spam"'
[rules.target]
notifier = ['^notifier\.gotify$']
)");
  SM_CHECK(parsed.set->has_delivery_rules());

  std::string title;
  std::string content = "spam";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);

  SM_CHECK(Apply(parsed.set.get(), &fields, "source.x"));
  SM_CHECK_TEXT(std::string(fields.content()), "spam");
  SM_CHECK_INT(parsed.set->counters(0).hits, 0);

  SM_CHECK(Deliver(parsed.set.get(), &fields, "source.x", "notifier.imgui"));
  SM_CHECK_TEXT(std::string(fields.content()), "clean");
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
  SM_CHECK_INT(parsed.set->counters(1).hits, 0);

  std::string other_content = "spam";
  std::string other_title;
  std::string other_summary;
  std::string other_topic;
  RuleFields other(other_title, other_content, other_summary, other_topic,
                   sm::Level::Info);
  SM_CHECK(!Deliver(parsed.set.get(), &other, "source.x", "notifier.gotify"));
  SM_CHECK_INT(parsed.set->counters(1).blocked, 1);
}

SM_TEST(rules_target, auto_summary_rejects_notifier_target) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "auto"
type = "auto_summary"
[rules.target]
notifier = ['^notifier\.imgui$']
)");
  SM_CHECK_INT(parsed.set->size(), 0);
  SM_CHECK_INT(parsed.set->skipped(), 1);
}

// ---------------------------------------------------------------------------
// 自动简介
// ---------------------------------------------------------------------------

SM_TEST(rules_auto_summary, default_file_generates_summary) {
  Parsed parsed = ParseRules(sm::host::DefaultRulesFileText());
  SM_CHECK(!parsed.failed);
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(parsed.set->rules()[0].type == sm::host::RuleType::AutoSummary);

  std::string title;
  std::string content = "  第一行   内容很长很长很长很长很长很长很长很长很长  ";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK(!fields.summary().empty());
  // 自动简介与 SDK 的 make_summary 完全一致（折叠空白、截断加省略号）。
  char expected[128];
  sm::make_summary(content.c_str(), 20, expected, sizeof(expected));
  SM_CHECK_TEXT(std::string(fields.summary()), std::string(expected));
  SM_CHECK_INT(parsed.set->counters(0).hits, 1);
}

SM_TEST(rules_auto_summary, max_codepoints_is_not_capped_to_sdk_default) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "auto"
type = "auto_summary"
max_codepoints = 500
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->skipped(), 0);
  SM_CHECK_INT(WarningCount(parsed.diagnostics), 0);

  // 超过 SDK 默认的 20 个码点、也超过旧的 128 字节固定缓冲。
  std::string content(300, 'a');
  content += " 0123456789 0123456789";
  std::string title;
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.summary()), content);
}

SM_TEST(rules_auto_summary, non_positive_max_codepoints_clamps_to_one) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "auto"
type = "auto_summary"
max_codepoints = 0
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(HasMessage(parsed.diagnostics, "小于 1"));

  std::string title;
  std::string content = "abc";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.summary()), "a…");
}

SM_TEST(rules_auto_summary, only_if_empty_respects_existing_summary) {
  Parsed parsed = ParseRules(sm::host::DefaultRulesFileText());

  std::string title;
  std::string content = "body";
  std::string summary = "已有";
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_TEXT(std::string(fields.summary()), "已有");
  SM_CHECK_INT(parsed.set->counters(0).hits, 0);
}

SM_TEST(rules_auto_summary, later_rules_can_match_generated_summary) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "auto"
type = "auto_summary"
[rules.rewrite]
summary = "ignored"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->rules()[0].type, sm::host::RuleType::AutoSummary);

  Parsed with_match = ParseRules(R"(
schema_version = 1
[[rules]]
id = "auto"
type = "auto_summary"
[[rules]]
id = "tag"
match = 'summary =~ "^hello"'
[rules.rewrite]
level = "warning"
)");
  SM_CHECK_INT(with_match.set->size(), 2);

  std::string title;
  std::string content = "hello world";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(with_match.set.get(), &fields, "s"));
  SM_CHECK(fields.level() == sm::Level::Warning);
}

// ---------------------------------------------------------------------------
// 启用覆盖与坏规则
// ---------------------------------------------------------------------------

SM_TEST(rules_enable, config_overrides_and_runtime_toggle) {
  const std::map<std::string, bool> enabled{{"r", false}};
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
drop = true
match = 'title =~ "x"'
)", enabled);

  SM_CHECK(!parsed.set->enabled(0));
  std::string title = "x";
  std::string content;
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  SM_CHECK(Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_INT(parsed.set->counters(0).hits, 0);

  SM_CHECK(parsed.set->SetEnabledById("r", true));
  SM_CHECK(parsed.set->enabled(0));
  SM_CHECK(!Apply(parsed.set.get(), &fields, "s"));
  SM_CHECK_INT(parsed.set->counters(0).blocked, 1);

  SM_CHECK(!parsed.set->SetEnabledById("missing", true));
}

SM_TEST(rules_diagnostics, bad_rules_are_skipped_individually) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "bad-regex"
match = 'title =~ "("'
[[rules]]
id = "good"
match = 'title =~ "^a$"'
[[rules]]
id = "good"
match = 'title =~ "^b$"'
[[rules]]
id = "drop-and-rewrite"
drop = true
match = 'title =~ "^c$"'
[rules.rewrite]
content = "x"
[[rules]]
id = "bad-level"
match = 'title =~ "^d$"'
[rules.rewrite]
level = "loud"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->skipped(), 4);
  SM_CHECK_TEXT(parsed.set->rules()[0].id, "good");
  SM_CHECK(WarningCount(parsed.diagnostics) >= 4);
}

SM_TEST(rules_diagnostics, whole_file_failures_are_reported) {
  Parsed syntax = ParseRules("schema_version = 1\n[[rules]\nid = \"x\"\n");
  SM_CHECK(syntax.failed);
  SM_CHECK_INT(syntax.set->size(), 0);

  Parsed future = ParseRules("schema_version = 2\n");
  SM_CHECK(future.failed);
  SM_CHECK_INT(future.set->size(), 0);

  Parsed bad_rules = ParseRules("schema_version = 1\nrules = 3\n");
  SM_CHECK(bad_rules.failed);
  SM_CHECK_INT(bad_rules.set->size(), 0);
}

SM_TEST(rules_diagnostics, unknown_keys_warn_but_do_not_skip) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
mystery = 1
[[rules]]
id = "r"
mystery = 2
[rules.target]
source = ["^s$"]
mystery = 3
[rules.rewrite]
mystery = 4
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK_INT(parsed.set->skipped(), 0);
  SM_CHECK(WarningCount(parsed.diagnostics) >= 4);
}

// ---------------------------------------------------------------------------
// 规则保存
// ---------------------------------------------------------------------------

SM_TEST(rules_save, nested_table_defaults_to_enabled) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "archive"
name = "归档"
match = 'topic =~ "^mail$"'
[rules.save]
path = "notify/{year}/{ruleid}.txt"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(parsed.set->rules()[0].save.enabled);
  SM_CHECK_TEXT(parsed.set->rules()[0].save.path,
                "notify/{year}/{ruleid}.txt");
  SM_CHECK_INT(WarningCount(parsed.diagnostics), 0);
}

SM_TEST(rules_save, inline_table_is_accepted) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
save = { enabled = true, path = "custom.txt" }
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(parsed.set->rules()[0].save.enabled);
  SM_CHECK_TEXT(parsed.set->rules()[0].save.path, "custom.txt");
}

SM_TEST(rules_save, disabled_rule_still_loads_and_warns_about_path) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
[rules.save]
enabled = false
path = "unused.txt"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(!parsed.set->rules()[0].save.enabled);
  SM_CHECK(HasMessage(parsed.diagnostics, "save.enabled = false"));
}

SM_TEST(rules_save, wrong_types_skip_the_rule) {
  Parsed not_table = ParseRules(R"(
schema_version = 1
[[rules]]
id = "a"
save = true
)");
  SM_CHECK_INT(not_table.set->size(), 0);

  Parsed bad_enabled = ParseRules(R"(
schema_version = 1
[[rules]]
id = "b"
[rules.save]
enabled = "yes"
)");
  SM_CHECK_INT(bad_enabled.set->size(), 0);

  Parsed bad_path = ParseRules(R"(
schema_version = 1
[[rules]]
id = "c"
[rules.save]
path = 3
)");
  SM_CHECK_INT(bad_path.set->size(), 0);
}

SM_TEST(rules_save, auto_summary_ignores_save) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "summary"
type = "auto_summary"
[rules.save]
enabled = true
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(!parsed.set->rules()[0].save.enabled);
  SM_CHECK(HasMessage(parsed.diagnostics, "auto_summary"));
}

SM_TEST(rules_save, unknown_placeholder_warns) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
[rules.save]
path = "notify/{date}-{mystery}.txt"
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(HasMessage(parsed.diagnostics, "mystery"));
}

SM_TEST(rules_save, source_hit_snapshot_follows_own_rewrite) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "first"
match = 'title =~ "^(.*)$"'
[rules.rewrite]
title = "A-$title.1"
content = "rewritten-body"
[rules.save]
enabled = true
[[rules]]
id = "second"
match = 'title =~ "^A-.*$"'
[rules.rewrite]
title = "B-$title.0"
)");
  SM_CHECK_INT(parsed.set->size(), 2);

  std::string title = "orig";
  std::string content = "body";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Warning);
  sm::host::RuleSaveHits hits;
  RuleDiagnostics warnings;
  SM_CHECK(parsed.set->ApplySource(&fields, "s", &warnings, &hits));

  SM_CHECK_INT(hits.size(), 1);
  if (hits.size() == 1) {
    SM_CHECK_INT(hits[0].rule_index, 0);
    SM_CHECK_TEXT(hits[0].title, "A-orig");
    SM_CHECK_TEXT(hits[0].content, "rewritten-body");
    SM_CHECK(hits[0].level == sm::Level::Warning);
  }
  // 第二条规则的改写不污染第一条的快照。
  SM_CHECK_TEXT(std::string(fields.title()), "B-A-orig");
}

SM_TEST(rules_save, drop_rule_still_reports_hit) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "drop"
drop = true
match = 'title =~ "^noise$"'
[rules.save]
enabled = true
)");
  std::string title = "noise";
  std::string content = "body";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  sm::host::RuleSaveHits hits;
  RuleDiagnostics warnings;
  SM_CHECK(!parsed.set->ApplySource(&fields, "s", &warnings, &hits));
  SM_CHECK_INT(hits.size(), 1);
  if (hits.size() == 1) {
    SM_CHECK_TEXT(hits[0].title, "noise");
    SM_CHECK_TEXT(hits[0].content, "body");
  }
}

SM_TEST(rules_save, unmatched_rule_reports_nothing) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "r"
match = 'title =~ "^hit$"'
[rules.save]
enabled = true
)");
  std::string title = "miss";
  std::string content = "body";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  sm::host::RuleSaveHits hits;
  RuleDiagnostics warnings;
  SM_CHECK(parsed.set->ApplySource(&fields, "s", &warnings, &hits));
  SM_CHECK(hits.empty());
}

SM_TEST(rules_save, delivery_hit_uses_delivery_rewrite) {
  Parsed parsed = ParseRules(R"(
schema_version = 1
[[rules]]
id = "deliver"
[rules.target]
notifier = ["^notifier.test$"]
[rules.rewrite]
summary = "delivered"
[rules.save]
enabled = true
)");
  SM_CHECK_INT(parsed.set->size(), 1);
  SM_CHECK(parsed.set->rules()[0].delivery_phase);

  std::string title = "x";
  std::string content = "body";
  std::string summary;
  std::string topic;
  RuleFields fields(title, content, summary, topic, sm::Level::Info);
  sm::host::RuleSaveHits hits;
  RuleDiagnostics warnings;
  SM_CHECK(parsed.set->ApplyDelivery(&fields, "s", "notifier.test", &warnings,
                                     &hits));
  SM_CHECK_INT(hits.size(), 1);
  if (hits.size() == 1) {
    SM_CHECK_INT(hits[0].rule_index, 0);
    SM_CHECK_TEXT(hits[0].summary, "delivered");
  }

  RuleFields other_fields(title, content, summary, topic, sm::Level::Info);
  sm::host::RuleSaveHits other_hits;
  parsed.set->ApplyDelivery(&other_fields, "s", "notifier.other", &warnings,
                            &other_hits);
  SM_CHECK(other_hits.empty());
}

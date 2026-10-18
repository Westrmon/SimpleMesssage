// SimpleMessage — 通知规则层：来源趟与投递趟的匹配、重写、丢弃
//
// 规则本身由 config/rules.toml 定义（只读，宿主只在文件缺失时生成默认模板）；
// 启用状态由 config.toml 的 [rules] 表覆盖，与 [plugins] 的语义一致。
//
// 这个模块刻意只依赖 sm::sdk 与 toml++：它必须能脱离窗口层、平台层和宿主日志
// 被测试（tests/test_rules.cpp）。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sm/notification.h"
#include "sm/types.h"

namespace sm {
namespace host {

struct RuleDiagnostic {
  enum class Severity : std::int32_t { Info = 0, Warn = 1, Error = 2 };

  Severity    severity = Severity::Info;
  std::string message;
};

using RuleDiagnostics = std::vector<RuleDiagnostic>;

enum class RuleType : std::int32_t {
  Match = 0,
  AutoSummary = 1,
};

struct RuleTarget {
  // 正则源码，整串匹配（regex_match）。空数组 = 不约束。
  std::vector<std::string> source;
  std::vector<std::string> notifier;  // 非空 => 投递趟
};

struct RuleSave {
  bool        enabled = false;
  // 占位符模板；空 = 默认路径 data_dir/notify/{ruleid}.txt。
  std::string path;
};

// 一条规则命中后交给宿主的保存快照。
// 快照在该规则自己的 rewrite 生效之后取得，因此同时带 rewrite 的规则
// 不会保存未重写的原文；同趟后续规则的改写也不会混进来。
struct RuleSaveHit {
  std::size_t rule_index = 0;
  std::string title;
  std::string content;
  std::string summary;
  sm::Level   level = sm::Level::Info;
};

using RuleSaveHits = std::vector<RuleSaveHit>;

// 匹配表达式：title/content/summary/topic 四个字段，=~ 与 !~ 的右侧是
// 双引号包住的正则；and / or / not 与括号组合，and 比 or 紧。
// 注意：target 是整串匹配，而表达式里的 =~ / !~ 是子串搜索
// （regex_search）；要整串匹配请自己写 ^...$。

struct RuleCounters {
  std::int64_t hits = 0;
  std::int64_t blocked = 0;
};

// RuleFields — 规则看到并修改的一组字段。
// 只有被重写的字段才复制；未重写的字段仍然指向调用方的 std::string。
class RuleFields {
 public:
  RuleFields(const std::string& title, const std::string& content,
             const std::string& summary, const std::string& topic, sm::Level level)
      : title_src_(&title),
        content_src_(&content),
        summary_src_(&summary),
        topic_src_(&topic),
        level_(level) {}

  std::string_view title() const {
    return has_title_ ? std::string_view(title_buf_) : std::string_view(*title_src_);
  }
  std::string_view content() const {
    return has_content_ ? std::string_view(content_buf_)
                        : std::string_view(*content_src_);
  }
  std::string_view summary() const {
    return has_summary_ ? std::string_view(summary_buf_)
                        : std::string_view(*summary_src_);
  }
  std::string_view topic() const { return std::string_view(*topic_src_); }

  sm::Level level() const { return level_; }

  void SetTitle(std::string value) {
    title_buf_ = std::move(value);
    has_title_ = true;
  }
  void SetContent(std::string value) {
    content_buf_ = std::move(value);
    has_content_ = true;
  }
  void SetSummary(std::string value) {
    summary_buf_ = std::move(value);
    has_summary_ = true;
  }
  void SetLevel(sm::Level value) {
    level_ = value;
    has_level_ = true;
  }

  bool changed() const {
    return has_title_ || has_content_ || has_summary_ || has_level_;
  }

  bool               has_title() const { return has_title_; }
  bool               has_content() const { return has_content_; }
  bool               has_summary() const { return has_summary_; }
  bool               has_level() const { return has_level_; }
  const std::string& title_buffer() const { return title_buf_; }
  const std::string& content_buffer() const { return content_buf_; }
  const std::string& summary_buffer() const { return summary_buf_; }

 private:
  const std::string* title_src_ = nullptr;
  const std::string* content_src_ = nullptr;
  const std::string* summary_src_ = nullptr;
  const std::string* topic_src_ = nullptr;

  std::string title_buf_;
  std::string content_buf_;
  std::string summary_buf_;
  bool        has_title_ = false;
  bool        has_content_ = false;
  bool        has_summary_ = false;
  bool        has_level_ = false;

  sm::Level level_ = sm::Level::Info;
};

struct Rule {
  std::string  id;
  std::string  name;
  RuleType     type = RuleType::Match;
  RuleTarget   target;
  bool         delivery_phase = false;

  // 匹配表达式原文（UI 展示用）与大小写开关。
  std::string match_text;
  bool        case_insensitive = false;

  // 重写模板。has_* = 该字段有 rewrite 项。
  bool        has_title = false;
  bool        has_content = false;
  bool        has_summary = false;
  bool        has_level = false;
  std::string title_template;
  std::string content_template;
  std::string summary_template;
  sm::Level   level = sm::Level::Info;

  bool drop = false;
  bool handle = false;

  // 命中后把这条通知追加写入文件；只对 type = match 生效。
  RuleSave save;

  // type = auto_summary 的参数。
  // 简介长度完全由规则决定：只要求 >= 1，没有上限（20 只是默认值）。
  std::string auto_from = "content";
  std::size_t auto_max_codepoints =
      static_cast<std::size_t>(sm::limits::kSummaryCodepoints);
  bool auto_only_if_empty = true;

  bool has_rewrite() const {
    return has_title || has_content || has_summary || has_level;
  }

  struct Compiled;
  std::shared_ptr<const Compiled> compiled;
};

// RuleSet — 已编译、只读、可跨线程共享的一份规则集。
// 启用标志与计数是 atomics，因此可以在不重建规则集的情况下切换。
class RuleSet {
 public:
  RuleSet();
  ~RuleSet();
  RuleSet(const RuleSet&) = delete;
  RuleSet& operator=(const RuleSet&) = delete;

  // 解析一份 rules.toml 文本。out_parse_failed 表示整份文件不可用
  // （语法错误、rules 不是数组、schema_version 过高）；单条规则出错只会
  // 跳过该条并记入 diagnostics。enabled 是 config.toml 的 [rules] 覆盖：
  // 表里没有的 id 默认启用。
  static std::shared_ptr<RuleSet> Parse(
      const std::string& text, const std::string& source_path,
      const std::map<std::string, bool>& enabled, RuleDiagnostics* out,
      bool* out_parse_failed);

  const std::vector<Rule>& rules() const { return rules_; }
  std::size_t              size() const { return rules_.size(); }
  bool                     empty() const { return rules_.empty(); }
  bool                     has_delivery_rules() const { return has_delivery_; }

  std::size_t              skipped() const { return skipped_; }
  const RuleDiagnostics&   diagnostics() const { return diagnostics_; }

  bool has_rule(const std::string& id) const;
  bool enabled(std::size_t index) const;
  bool SetEnabledByIndex(std::size_t index, bool value);
  bool SetEnabledById(const std::string& id, bool value);

  RuleCounters counters(std::size_t index) const;

  // 来源趟：返回 false 表示通知被 drop，调用方应丢弃它。
  // runtime_warnings 收集运行时问题（例如重写结果为空），可为 nullptr。
  // save_hits 非空时，收集启用了 save 的命中规则及其 rewrite 后的字段快照。
  bool ApplySource(RuleFields* fields, const std::string& source_plugin,
                   RuleDiagnostics* runtime_warnings,
                   RuleSaveHits* save_hits = nullptr);

  // 投递趟：返回 false 表示这一条不对该 Notifier 投递。
  bool ApplyDelivery(RuleFields* fields, const std::string& source_plugin,
                     const std::string& notifier_id,
                     RuleDiagnostics* runtime_warnings,
                     RuleSaveHits* save_hits = nullptr);

 private:
  struct Runtime;

  std::vector<Rule>                            rules_;
  std::vector<std::unique_ptr<Runtime>>        runtime_;
  RuleDiagnostics                              diagnostics_;
  std::size_t                                  skipped_ = 0;
  bool                                         has_delivery_ = false;
};

// 首次运行且 rules.toml 不存在时写入的默认内容。
const char* DefaultRulesFileText();

}  // namespace host
}  // namespace sm

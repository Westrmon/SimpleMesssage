// SimpleMessage — 通知规则层实现
//
// 设计约束（见 docs/02-宿主设计.md 的"规则层"一节）：
//   - 来源趟规则在 Admit 里执行，改写进入历史，drop 后通知不存在；
//   - 投递趟规则在 Deliver 前执行，只影响该 Notifier 的 on_notify 视图；
//   - 规则按文件顺序累积求值，handle 终止当前趟，drop 总是终止当前趟；
//   - 同一字段在一条规则里最多一个正向 =~，捕获组用 $field.N 引用。
#include "host/rules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/summary.h"

#include "host/placeholders.h"
#include "host/plugin_id.h"

#include <toml.hpp>

namespace sm {
namespace host {

namespace {

constexpr std::size_t kMaxRules = 256;
constexpr std::size_t kMaxRegexBytes = 1024;
constexpr std::size_t kMaxRuleIdBytes = 64;
constexpr std::size_t kMaxSavePathBytes = 1024;

constexpr int kFieldTitle = 0;
constexpr int kFieldContent = 1;
constexpr int kFieldSummary = 2;
constexpr int kFieldTopic = 3;

std::string RulePrefix(std::size_t index, const std::string& id) {
  std::string text = "第 " + std::to_string(index) + " 条规则";
  if (!id.empty()) text += "（" + id + "）";
  return text;
}

void AddDiag(RuleDiagnostics* out, RuleDiagnostic::Severity severity,
             std::string message) {
  if (out == nullptr) return;
  out->push_back(RuleDiagnostic{severity, std::move(message)});
}

int FieldIndex(std::string_view name) {
  if (name == "title") return kFieldTitle;
  if (name == "content") return kFieldContent;
  if (name == "summary") return kFieldSummary;
  if (name == "topic") return kFieldTopic;
  return -1;
}

const char* FieldName(int field) {
  switch (field) {
    case kFieldTitle: return "title";
    case kFieldContent: return "content";
    case kFieldSummary: return "summary";
    case kFieldTopic: return "topic";
    default: return "?";
  }
}

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// ---------------------------------------------------------------------------
// 模板：把 "单号 $content.1" 编译成字面量/捕获组片段
// ---------------------------------------------------------------------------

struct TemplateSegment {
  bool        is_capture = false;
  int         field = 0;
  int         group = 0;
  std::string literal;
};

struct CompiledTemplate {
  std::vector<TemplateSegment> segments;
  bool                         has_capture = false;
};

bool BuildTemplate(const std::string& text, int target_field,
                   const std::array<int, 4>& positive_count,
                   const std::array<int, 4>& group_count,
                   CompiledTemplate* out, std::string* error) {
  out->segments.clear();
  out->has_capture = false;

  std::string literal;
  const auto flush = [&]() {
    if (literal.empty()) return;
    TemplateSegment segment;
    segment.literal = std::move(literal);
    out->segments.push_back(std::move(segment));
    literal.clear();
  };

  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '$') {
      literal.push_back(text[i]);
      ++i;
      continue;
    }

    flush();
    if (i + 1 >= text.size()) {
      *error = "$ 后面缺少内容（字面 $ 请写 $$）";
      return false;
    }
    if (text[i + 1] == '$') {
      literal.push_back('$');
      i += 2;
      continue;
    }

    const std::size_t field_start = i + 1;
    std::size_t       dot = field_start;
    while (dot < text.size() && text[dot] != '.' && text[dot] != '$') ++dot;
    if (dot >= text.size() || text[dot] != '.') {
      *error = "捕获组要写成 $字段.组号，例如 $content.1";
      return false;
    }

    const std::string_view field_text(text.data() + field_start, dot - field_start);
    const int field = FieldIndex(field_text);
    if (field < 0) {
      *error = "未知字段 $" + std::string(field_text) +
               "（可用 title/content/summary/topic）";
      return false;
    }

    std::size_t digit_start = dot + 1;
    std::size_t digit_end = digit_start;
    while (digit_end < text.size() && std::isdigit(static_cast<unsigned char>(text[digit_end])) != 0) {
      ++digit_end;
    }
    if (digit_end == digit_start || digit_end - digit_start > 1) {
      *error = "$" + std::string(field_text) + ". 后面需要 0–9 的单个组号";
      return false;
    }
    const int group = text[digit_start] - '0';

    if (positive_count[field] <= 0) {
      *error = "模板引用了 $" + std::string(field_text) +
               " 的捕获组，但表达式里没有该字段的 =~";
      return false;
    }
    if (group > group_count[field]) {
      *error = "$" + std::string(field_text) + "." + std::to_string(group) +
               " 超出捕获组数量（" + std::to_string(group_count[field]) + "）";
      return false;
    }

    TemplateSegment segment;
    segment.is_capture = true;
    segment.field = field;
    segment.group = group;
    out->segments.push_back(std::move(segment));
    out->has_capture = true;

    (void)target_field;
    i = digit_end;
  }

  flush();
  return true;
}

struct EvalContext {
  std::array<std::cmatch, 4> matches;
  std::array<bool, 4>        has{{false, false, false, false}};
};

std::string ExpandTemplate(const CompiledTemplate& text, EvalContext* context,
                           bool* missing_capture) {
  std::string out;
  for (const TemplateSegment& segment : text.segments) {
    if (!segment.is_capture) {
      out += segment.literal;
      continue;
    }
    if (!context->has[static_cast<std::size_t>(segment.field)]) {
      *missing_capture = true;
      continue;
    }
    const std::cmatch& match = context->matches[static_cast<std::size_t>(segment.field)];
    if (segment.group >= 0 &&
        static_cast<std::size_t>(segment.group) < match.size()) {
      out += match[static_cast<std::size_t>(segment.group)].str();
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// 表达式：字段 =~ / !~ 正则，and / or / not，括号
// ---------------------------------------------------------------------------

struct ExprNode {
  virtual ~ExprNode() = default;
  virtual bool Eval(const std::array<std::string_view, 4>& values,
                    EvalContext* context) const = 0;
};

struct AndNode final : ExprNode {
  std::unique_ptr<ExprNode> lhs;
  std::unique_ptr<ExprNode> rhs;
  bool Eval(const std::array<std::string_view, 4>& values,
            EvalContext* context) const override {
    return lhs->Eval(values, context) && rhs->Eval(values, context);
  }
};

struct OrNode final : ExprNode {
  std::unique_ptr<ExprNode> lhs;
  std::unique_ptr<ExprNode> rhs;
  bool Eval(const std::array<std::string_view, 4>& values,
            EvalContext* context) const override {
    return lhs->Eval(values, context) || rhs->Eval(values, context);
  }
};

struct NotNode final : ExprNode {
  std::unique_ptr<ExprNode> inner;
  bool Eval(const std::array<std::string_view, 4>& values,
            EvalContext* context) const override {
    return !inner->Eval(values, context);
  }
};

struct CompareNode final : ExprNode {
  int                                field = 0;
  bool                               positive = true;
  std::shared_ptr<const std::regex>  regex;

  bool Eval(const std::array<std::string_view, 4>& values,
            EvalContext* context) const override {
    const std::string_view text = values[static_cast<std::size_t>(field)];
    const char* begin = text.data();
    const char* end = begin + text.size();

    std::cmatch match;
    const bool found = std::regex_search(begin, end, match, *regex);
    if (!positive) return !found;

    if (!found) return false;
    context->matches[static_cast<std::size_t>(field)] = std::move(match);
    context->has[static_cast<std::size_t>(field)] = true;
    return true;
  }
};

class ExprParser {
 public:
  ExprParser(std::string_view text, bool case_insensitive)
      : text_(text), case_insensitive_(case_insensitive) {}

  bool Parse(std::unique_ptr<ExprNode>* out, std::string* error) {
    out->reset();
    Advance();
    if (current_.kind == TokenKind::End) return true;  // 空表达式 = 恒真

    std::unique_ptr<ExprNode> node = ParseOr(error);
    if (!node) return false;
    if (current_.kind != TokenKind::End) {
      *error = "表达式末尾有多余内容";
      return false;
    }
    *out = std::move(node);
    return true;
  }

  const std::array<int, 4>& positive_count() const { return positive_count_; }
  const std::array<int, 4>& group_count() const { return group_count_; }

 private:
  enum class TokenKind {
    End,
    Ident,
    String,
    And,
    Or,
    Not,
    MatchOp,
    NotMatchOp,
    LParen,
    RParen,
    Invalid,
  };

  struct Token {
    TokenKind   kind = TokenKind::End;
    std::string text;
    std::size_t position = 0;
  };

  void Advance() { current_ = NextToken(); }

  Token NextToken() {
    while (position_ < text_.size() && IsSpace(text_[position_])) ++position_;

    Token token;
    token.position = position_;
    if (position_ >= text_.size()) return token;

    const char c = text_[position_];
    if (c == '(') {
      ++position_;
      token.kind = TokenKind::LParen;
      return token;
    }
    if (c == ')') {
      ++position_;
      token.kind = TokenKind::RParen;
      return token;
    }
    if (c == '=' && position_ + 1 < text_.size() && text_[position_ + 1] == '~') {
      position_ += 2;
      token.kind = TokenKind::MatchOp;
      return token;
    }
    if (c == '!' && position_ + 1 < text_.size() && text_[position_ + 1] == '~') {
      position_ += 2;
      token.kind = TokenKind::NotMatchOp;
      return token;
    }
    if (c == '"') return ReadString();

    if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
      const std::size_t begin = position_;
      while (position_ < text_.size()) {
        const char ch = text_[position_];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') break;
        ++position_;
      }
      token.text = std::string(text_.substr(begin, position_ - begin));
      if (token.text == "and") {
        token.kind = TokenKind::And;
      } else if (token.text == "or") {
        token.kind = TokenKind::Or;
      } else if (token.text == "not") {
        token.kind = TokenKind::Not;
      } else {
        token.kind = TokenKind::Ident;
      }
      return token;
    }

    token.kind = TokenKind::Invalid;
    token.text = std::string(1, c);
    ++position_;
    return token;
  }

  Token ReadString() {
    Token token;
    token.position = position_;
    ++position_;  // 开头的双引号

    std::string value;
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if (c == '"') {
        ++position_;
        token.kind = TokenKind::String;
        token.text = std::move(value);
        return token;
      }
      if (c == '\\' && position_ + 1 < text_.size()) {
        const char next = text_[position_ + 1];
        if (next == '"' || next == '\\') {
          value.push_back(next);
          position_ += 2;
          continue;
        }
        // 其余反斜杠原样交给 std::regex（\d、\s、\[ ...）。
        value.push_back('\\');
        value.push_back(next);
        position_ += 2;
        continue;
      }
      value.push_back(c);
      ++position_;
    }

    token.kind = TokenKind::Invalid;
    token.text = "字符串没有闭合";
    return token;
  }

  std::unique_ptr<ExprNode> ParseOr(std::string* error) {
    std::unique_ptr<ExprNode> node = ParseAnd(error);
    if (!node) return nullptr;

    while (current_.kind == TokenKind::Or) {
      Advance();
      std::unique_ptr<ExprNode> rhs = ParseAnd(error);
      if (!rhs) return nullptr;
      auto combined = std::make_unique<OrNode>();
      combined->lhs = std::move(node);
      combined->rhs = std::move(rhs);
      node = std::move(combined);
    }
    return node;
  }

  std::unique_ptr<ExprNode> ParseAnd(std::string* error) {
    std::unique_ptr<ExprNode> node = ParseNot(error);
    if (!node) return nullptr;

    while (current_.kind == TokenKind::And) {
      Advance();
      std::unique_ptr<ExprNode> rhs = ParseNot(error);
      if (!rhs) return nullptr;
      auto combined = std::make_unique<AndNode>();
      combined->lhs = std::move(node);
      combined->rhs = std::move(rhs);
      node = std::move(combined);
    }
    return node;
  }

  std::unique_ptr<ExprNode> ParseNot(std::string* error) {
    if (current_.kind == TokenKind::Not) {
      Advance();
      std::unique_ptr<ExprNode> inner = ParseNot(error);
      if (!inner) return nullptr;
      auto node = std::make_unique<NotNode>();
      node->inner = std::move(inner);
      return node;
    }
    return ParsePrimary(error);
  }

  std::unique_ptr<ExprNode> ParsePrimary(std::string* error) {
    if (current_.kind == TokenKind::LParen) {
      Advance();
      std::unique_ptr<ExprNode> inner = ParseOr(error);
      if (!inner) return nullptr;
      if (current_.kind != TokenKind::RParen) {
        *error = "缺少右括号";
        return nullptr;
      }
      Advance();
      return inner;
    }
    return ParseCompare(error);
  }

  std::unique_ptr<ExprNode> ParseCompare(std::string* error) {
    if (current_.kind != TokenKind::Ident) {
      *error = "这里需要字段名 title/content/summary/topic";
      return nullptr;
    }
    const int field = FieldIndex(current_.text);
    if (field < 0) {
      *error = "未知字段 " + current_.text +
               "（可用 title/content/summary/topic）";
      return nullptr;
    }
    const std::size_t field_position = current_.position;
    Advance();

    if (current_.kind != TokenKind::MatchOp &&
        current_.kind != TokenKind::NotMatchOp) {
      *error = "字段后面需要 =~ 或 !~";
      return nullptr;
    }
    const bool positive = current_.kind == TokenKind::MatchOp;
    Advance();

    if (current_.kind != TokenKind::String) {
      *error = "=~ / !~ 后面需要双引号字符串";
      return nullptr;
    }

    const std::string pattern = current_.text;
    Advance();

    std::shared_ptr<const std::regex> regex;
    std::string regex_error;
    if (!CompileRegex(pattern, case_insensitive_, &regex, &regex_error)) {
      *error = "正则编译失败（位置 " + std::to_string(field_position) +
               "）：" + regex_error;
      return nullptr;
    }

    if (positive) {
      ++positive_count_[static_cast<std::size_t>(field)];
      if (positive_count_[static_cast<std::size_t>(field)] > 1) {
        *error = std::string("字段 ") + FieldName(field) +
                 " 在一条规则里最多出现一次 =~";
        return nullptr;
      }
      group_count_[static_cast<std::size_t>(field)] =
          static_cast<int>(regex->mark_count());
    }

    auto node = std::make_unique<CompareNode>();
    node->field = field;
    node->positive = positive;
    node->regex = std::move(regex);
    return node;
  }

  static bool CompileRegex(const std::string& pattern, bool case_insensitive,
                           std::shared_ptr<const std::regex>* out,
                           std::string* error) {
    if (pattern.size() > kMaxRegexBytes) {
      *error = "正则超过 " + std::to_string(kMaxRegexBytes) + " 字节";
      return false;
    }
    std::regex_constants::syntax_option_type flags =
        std::regex_constants::ECMAScript;
    if (case_insensitive) flags |= std::regex_constants::icase;
    try {
      *out = std::make_shared<const std::regex>(pattern, flags);
      return true;
    } catch (const std::regex_error& e) {
      *error = e.what();
      return false;
    }
  }

  std::string_view text_;
  bool             case_insensitive_ = false;
  std::size_t      position_ = 0;
  Token            current_;
  std::array<int, 4> positive_count_{{0, 0, 0, 0}};
  std::array<int, 4> group_count_{{0, 0, 0, 0}};
};

// ---------------------------------------------------------------------------
// 编译后的规则
// ---------------------------------------------------------------------------

using RegexPtr = std::shared_ptr<const std::regex>;

bool CompileTargetRegex(const std::string& pattern, RegexPtr* out,
                        std::string* error) {
  if (pattern.size() > kMaxRegexBytes) {
    *error = "target 正则超过 " + std::to_string(kMaxRegexBytes) + " 字节";
    return false;
  }
  try {
    *out = std::make_shared<const std::regex>(pattern,
                                              std::regex_constants::ECMAScript);
    return true;
  } catch (const std::regex_error& e) {
    *error = e.what();
    return false;
  }
}

}  // namespace

struct Rule::Compiled {
  std::vector<RegexPtr> source;
  std::vector<RegexPtr> notifier;

  std::unique_ptr<ExprNode> expr;  // null = 恒真

  std::array<int, 4> positive_count{{0, 0, 0, 0}};
  std::array<int, 4> group_count{{0, 0, 0, 0}};

  CompiledTemplate title;
  CompiledTemplate content;
  CompiledTemplate summary;
};

struct RuleSet::Runtime {
  std::atomic<bool>          enabled{true};
  std::atomic<std::int64_t>  hits{0};
  std::atomic<std::int64_t>  blocked{0};
  std::atomic<bool>          warned_missing_capture{false};
  std::atomic<bool>          warned_empty_rewrite{false};
};

namespace {

RuleType ParseRuleType(const toml::node* node, bool* ok,
                       const std::string& prefix, RuleDiagnostics* out) {
  *ok = true;
  if (node == nullptr) return RuleType::Match;

  const auto* value = node->as_string();
  if (value == nullptr) {
    AddDiag(out, RuleDiagnostic::Severity::Warn,
            prefix + " 的 type 不是字符串，已跳过");
    *ok = false;
    return RuleType::Match;
  }

  const std::string text = value->get();
  if (text.empty() || text == "match") return RuleType::Match;
  if (text == "auto_summary") return RuleType::AutoSummary;

  AddDiag(out, RuleDiagnostic::Severity::Warn,
          prefix + " 的 type \"" + text + "\" 不是 match/auto_summary，已跳过");
  *ok = false;
  return RuleType::Match;
}

bool ReadStringArray(const toml::table& table, const char* key,
                     std::vector<std::string>* out, const std::string& prefix,
                     RuleDiagnostics* diagnostics) {
  const toml::node* node = table.get(key);
  if (node == nullptr) return true;

  const toml::array* array = node->as_array();
  if (array == nullptr) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            prefix + " 的 target." + key + " 不是数组，已跳过");
    return false;
  }

  for (auto&& element : *array) {
    const auto* value = element.as_string();
    if (value == nullptr) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 target." + key + " 里有非字符串项，已跳过");
      return false;
    }
    const std::string pattern = value->get();
    if (pattern.size() > kMaxRegexBytes) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 target." + key + " 里有超过 " +
                  std::to_string(kMaxRegexBytes) + " 字节的正则，已跳过");
      return false;
    }
    out->push_back(pattern);
  }
  return true;
}

void WarnUnknownRuleKeys(const toml::table& table, const std::string& prefix,
                         std::initializer_list<const char*> allowed,
                         RuleDiagnostics* diagnostics) {
  for (auto&& entry : table) {
    const std::string key(entry.first.str());
    bool known = false;
    for (const char* name : allowed) {
      if (key == name) {
        known = true;
        break;
      }
    }
    if (!known) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的未知键 " + key + "，已忽略");
    }
  }
}

bool BuildRule(const toml::table& table, std::size_t index,
               const std::map<std::string, bool>& enabled,
               RuleDiagnostics* diagnostics, Rule* out, bool* out_enabled) {
  std::string prefix = RulePrefix(index, "");

  const toml::node* id_node = table.get("id");
  if (id_node == nullptr) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            prefix + " 缺少 id，已跳过");
    return false;
  }
  const auto* id_value = id_node->as_string();
  if (id_value == nullptr) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            prefix + " 的 id 不是字符串，已跳过");
    return false;
  }
  const std::string id = id_value->get();
  prefix = RulePrefix(index, id);
  if (id.empty() || id.size() > kMaxRuleIdBytes) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            prefix + " 的 id 必须为 1–" + std::to_string(kMaxRuleIdBytes) +
                " 字节，已跳过");
    return false;
  }

  Rule rule;
  rule.id = id;
  rule.name = id;
  if (const toml::node* name_node = table.get("name")) {
    const auto* name_value = name_node->as_string();
    if (name_value == nullptr) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 name 不是字符串，改用 id");
    } else {
      const std::string name = name_value->get();
      if (!name.empty()) rule.name = name;
    }
  }

  bool type_ok = true;
  rule.type = ParseRuleType(table.get("type"), &type_ok, prefix, diagnostics);
  if (!type_ok) return false;

  WarnUnknownRuleKeys(
      table, prefix,
      {"id", "name", "type", "target", "match", "case_insensitive", "rewrite",
       "drop", "handle", "save", "from", "max_codepoints", "only_if_empty"},
      diagnostics);

  if (const toml::node* target_node = table.get("target")) {
    const toml::table* target = target_node->as_table();
    if (target == nullptr) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 target 不是表，已跳过");
      return false;
    }
    WarnUnknownRuleKeys(*target, prefix + " 的 target", {"source", "notifier"},
                        diagnostics);
    if (!ReadStringArray(*target, "source", &rule.target.source, prefix,
                         diagnostics) ||
        !ReadStringArray(*target, "notifier", &rule.target.notifier, prefix,
                         diagnostics)) {
      return false;
    }
  }

  rule.delivery_phase = !rule.target.notifier.empty();
  if (rule.type == RuleType::AutoSummary && rule.delivery_phase) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            prefix + " 是 auto_summary，不能写 notifier 目标（它必须在来源趟执行），"
                     "已跳过");
    return false;
  }

  auto compiled = std::make_shared<Rule::Compiled>();

  std::string regex_error;
  for (const std::string& pattern : rule.target.source) {
    RegexPtr regex;
    if (!CompileTargetRegex(pattern, &regex, &regex_error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 target.source 正则 \"" + pattern +
                  "\" 编译失败：" + regex_error + "，已跳过");
      return false;
    }
    compiled->source.push_back(std::move(regex));
  }
  for (const std::string& pattern : rule.target.notifier) {
    RegexPtr regex;
    if (!CompileTargetRegex(pattern, &regex, &regex_error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 target.notifier 正则 \"" + pattern +
                  "\" 编译失败：" + regex_error + "，已跳过");
      return false;
    }
    compiled->notifier.push_back(std::move(regex));
  }

  if (rule.type == RuleType::Match) {
    if (const toml::node* match_node = table.get("match")) {
      const auto* match_value = match_node->as_string();
      if (match_value == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 match 不是字符串，已跳过");
        return false;
      }
      rule.match_text = match_value->get();
    }

    if (const toml::node* ci_node = table.get("case_insensitive")) {
      const auto* ci_value = ci_node->as_boolean();
      if (ci_value == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 case_insensitive 不是布尔值，已跳过");
        return false;
      }
      rule.case_insensitive = ci_value->get();
    }

    if (const toml::node* rewrite_node = table.get("rewrite")) {
      const toml::table* rewrite = rewrite_node->as_table();
      if (rewrite == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 rewrite 不是表，已跳过");
        return false;
      }
      WarnUnknownRuleKeys(*rewrite, prefix + " 的 rewrite",
                          {"title", "content", "summary", "level"}, diagnostics);

      const auto read_template = [&](const char* key, bool* has,
                                     std::string* text) -> bool {
        const toml::node* node = rewrite->get(key);
        if (node == nullptr) return true;
        const auto* value = node->as_string();
        if (value == nullptr) {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 rewrite." + key + " 不是字符串，已跳过");
          return false;
        }
        *has = true;
        *text = value->get();
        return true;
      };
      if (!read_template("title", &rule.has_title, &rule.title_template) ||
          !read_template("content", &rule.has_content, &rule.content_template) ||
          !read_template("summary", &rule.has_summary, &rule.summary_template)) {
        return false;
      }

      if (const toml::node* level_node = rewrite->get("level")) {
        const auto* level_value = level_node->as_string();
        if (level_value == nullptr) {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 rewrite.level 不是字符串，已跳过");
          return false;
        }
        const std::string text = level_value->get();
        if (text == "debug") {
          rule.level = sm::Level::Debug;
        } else if (text == "info") {
          rule.level = sm::Level::Info;
        } else if (text == "warning") {
          rule.level = sm::Level::Warning;
        } else if (text == "error") {
          rule.level = sm::Level::Error;
        } else if (text == "critical") {
          rule.level = sm::Level::Critical;
        } else {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 rewrite.level \"" + text +
                      "\" 不是 debug/info/warning/error/critical，已跳过");
          return false;
        }
        rule.has_level = true;
      }
    }

    const auto read_bool = [&](const char* key, bool* value) -> bool {
      const toml::node* node = table.get(key);
      if (node == nullptr) return true;
      const auto* boolean = node->as_boolean();
      if (boolean == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 " + key + " 不是布尔值，已跳过");
        return false;
      }
      *value = boolean->get();
      return true;
    };
    if (!read_bool("drop", &rule.drop) || !read_bool("handle", &rule.handle)) {
      return false;
    }

    if (rule.drop && rule.has_rewrite()) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 同时写了 drop 与 rewrite，已跳过（drop 的规则不应再重写）");
      return false;
    }

    if (const toml::node* save_node = table.get("save")) {
      const toml::table* save = save_node->as_table();
      if (save == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 save 不是表，已跳过");
        return false;
      }
      WarnUnknownRuleKeys(*save, prefix + " 的 save", {"enabled", "path"},
                          diagnostics);

      if (const toml::node* enabled_node = save->get("enabled")) {
        const auto* enabled_value = enabled_node->as_boolean();
        if (enabled_value == nullptr) {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 save.enabled 不是布尔值，已跳过");
          return false;
        }
        rule.save.enabled = enabled_value->get();
      } else {
        // 写了 [rules.save] 就表示想保存；显式 enabled = false 才关闭。
        rule.save.enabled = true;
      }

      if (const toml::node* path_node = save->get("path")) {
        const auto* path_value = path_node->as_string();
        if (path_value == nullptr) {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 save.path 不是字符串，已跳过");
          return false;
        }
        rule.save.path = path_value->get();
      }

      if (rule.save.path.size() > kMaxSavePathBytes) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 save.path 超过 " +
                    std::to_string(kMaxSavePathBytes) + " 字节，保存已关闭");
        rule.save.enabled = false;
      } else if (rule.save.enabled) {
        for (const std::string& name : FindPlaceholderNames(rule.save.path)) {
          if (!IsKnownNotificationPlaceholder(name)) {
            AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                    prefix + " 的 save.path 含未知占位符 {" + name +
                        "}，将原样保留");
          }
        }
      } else if (!rule.save.path.empty()) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 save.enabled = false，save.path 会被忽略");
      }
    }

    ExprParser parser(rule.match_text, rule.case_insensitive);
    std::string error;
    if (!parser.Parse(&compiled->expr, &error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 match 表达式无效：" + error + "，已跳过");
      return false;
    }
    compiled->positive_count = parser.positive_count();
    compiled->group_count = parser.group_count();

    if (rule.has_title &&
        !BuildTemplate(rule.title_template, kFieldTitle,
                       compiled->positive_count, compiled->group_count,
                       &compiled->title, &error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 rewrite.title 模板无效：" + error + "，已跳过");
      return false;
    }
    if (rule.has_content &&
        !BuildTemplate(rule.content_template, kFieldContent,
                       compiled->positive_count, compiled->group_count,
                       &compiled->content, &error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 rewrite.content 模板无效：" + error + "，已跳过");
      return false;
    }
    if (rule.has_summary &&
        !BuildTemplate(rule.summary_template, kFieldSummary,
                       compiled->positive_count, compiled->group_count,
                       &compiled->summary, &error)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              prefix + " 的 rewrite.summary 模板无效：" + error + "，已跳过");
      return false;
    }
  } else {
    // auto_summary 只接受自己的参数；其它字段是明显的误用，提示后忽略。
    for (const char* key :
         {"match", "case_insensitive", "rewrite", "drop", "handle", "save"}) {
      if (table.get(key) != nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 是 auto_summary，" + key + " 会被忽略");
      }
    }

    if (const toml::node* from_node = table.get("from")) {
      const auto* from_value = from_node->as_string();
      if (from_value == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 from 不是字符串，改用 content");
      } else {
        const std::string from = from_value->get();
        if (from != "content") {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 from \"" + from + "\" 不支持（v1 只接受 content），"
                           "改用 content");
        }
      }
    }

    if (const toml::node* max_node = table.get("max_codepoints")) {
      const auto* max_value = max_node->as_integer();
      if (max_value == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 max_codepoints 不是整数，改用 20");
      } else {
        // 上限完全由规则决定；宿主只兜住“小于 1”这种无法取码点的值。
        std::int64_t count = max_value->get();
        if (count < 1) {
          AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                  prefix + " 的 max_codepoints = " + std::to_string(count) +
                      " 小于 1，夹到 1");
          count = 1;
        }
        rule.auto_max_codepoints = static_cast<std::size_t>(count);
      }
    }

    if (const toml::node* only_node = table.get("only_if_empty")) {
      const auto* only_value = only_node->as_boolean();
      if (only_value == nullptr) {
        AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
                prefix + " 的 only_if_empty 不是布尔值，改用 true");
      } else {
        rule.auto_only_if_empty = only_value->get();
      }
    }
  }

  rule.compiled = std::move(compiled);

  *out = std::move(rule);
  const auto it = enabled.find(id);
  *out_enabled = (it == enabled.end()) ? true : it->second;
  return true;
}

}  // namespace

RuleSet::RuleSet() = default;
RuleSet::~RuleSet() = default;

std::shared_ptr<RuleSet> RuleSet::Parse(
    const std::string& text, const std::string& source_path,
    const std::map<std::string, bool>& enabled, RuleDiagnostics* out,
    bool* out_parse_failed) {
  auto set = std::make_shared<RuleSet>();
  if (out_parse_failed != nullptr) *out_parse_failed = false;
  RuleDiagnostics* const diagnostics = &set->diagnostics_;
  const auto finish = [&]() {
    if (out != nullptr) *out = set->diagnostics_;
    return set;
  };

  toml::table root;
  try {
    root = toml::parse(text, source_path);
  } catch (const toml::parse_error& e) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Error,
            "规则文件解析失败：" + std::string(e.what()));
    if (out_parse_failed != nullptr) *out_parse_failed = true;
    return finish();
  }

  if (const toml::node* version_node = root.get("schema_version")) {
    const auto* version = version_node->as_integer();
    if (version == nullptr) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              "schema_version 不是整数，按 1 处理");
    } else if (version->get() > 1) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Error,
              "rules.toml 的 schema_version 为 " +
                  std::to_string(version->get()) +
                  "，高于本宿主支持的 1；本次不加载任何规则");
      if (out_parse_failed != nullptr) *out_parse_failed = true;
      return finish();
    }
  } else {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
            "rules.toml 缺少 schema_version，按 1 处理");
  }

  for (auto&& entry : root) {
    const std::string key(entry.first.str());
    if (key != "schema_version" && key != "rules") {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              "rules.toml 里的未知顶层键 " + key + "，已忽略");
    }
  }

  const toml::node* rules_node = root.get("rules");
  if (rules_node == nullptr) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Info,
            "rules.toml 里没有 [[rules]]");
    return finish();
  }
  const toml::array* array = rules_node->as_array();
  if (array == nullptr) {
    AddDiag(diagnostics, RuleDiagnostic::Severity::Error,
            "rules.toml 的 rules 不是数组；本次不加载任何规则");
    if (out_parse_failed != nullptr) *out_parse_failed = true;
    return finish();
  }

  std::size_t index = 0;
  for (auto&& element : *array) {
    ++index;
    if (set->rules_.size() >= kMaxRules) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              "规则数量超过 " + std::to_string(kMaxRules) + " 条，其余已忽略");
      break;
    }

    const toml::table* table = element.as_table();
    if (table == nullptr) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              RulePrefix(index, "") + " 不是表，已跳过");
      ++set->skipped_;
      continue;
    }

    Rule rule;
    bool rule_enabled = true;
    if (!BuildRule(*table, index, enabled, diagnostics, &rule, &rule_enabled)) {
      ++set->skipped_;
      continue;
    }
    if (set->has_rule(rule.id)) {
      AddDiag(diagnostics, RuleDiagnostic::Severity::Warn,
              RulePrefix(index, rule.id) + " 的 id 重复，已跳过");
      ++set->skipped_;
      continue;
    }

    auto runtime = std::make_unique<Runtime>();
    runtime->enabled.store(rule_enabled, std::memory_order_relaxed);
    set->runtime_.push_back(std::move(runtime));
    if (rule.delivery_phase) set->has_delivery_ = true;
    set->rules_.push_back(std::move(rule));
  }

  return finish();
}

bool RuleSet::has_rule(const std::string& id) const {
  for (const Rule& rule : rules_) {
    if (rule.id == id) return true;
  }
  return false;
}

bool RuleSet::enabled(std::size_t index) const {
  if (index >= runtime_.size()) return false;
  return runtime_[index]->enabled.load(std::memory_order_relaxed);
}

bool RuleSet::SetEnabledByIndex(std::size_t index, bool value) {
  if (index >= runtime_.size()) return false;
  runtime_[index]->enabled.store(value, std::memory_order_relaxed);
  return true;
}

bool RuleSet::SetEnabledById(const std::string& id, bool value) {
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    if (rules_[i].id == id) return SetEnabledByIndex(i, value);
  }
  return false;
}

RuleCounters RuleSet::counters(std::size_t index) const {
  RuleCounters out;
  if (index >= runtime_.size()) return out;
  out.hits = runtime_[index]->hits.load(std::memory_order_relaxed);
  out.blocked = runtime_[index]->blocked.load(std::memory_order_relaxed);
  return out;
}

namespace {

bool MatchAnyTarget(const std::vector<RegexPtr>& patterns,
                    const std::string& name) {
  if (patterns.empty()) return true;
  for (const RegexPtr& regex : patterns) {
    if (std::regex_match(name, *regex)) return true;
  }
  return false;
}

bool SourceTargetMatches(const Rule& rule, const std::string& source_plugin) {
  if (MatchAnyTarget(rule.compiled->source, source_plugin)) return true;
  // 规则写类型 id（^source.email$）时命中该类型的所有实例；写实例 id
  // （^source.email@2$）时只命中那一个。Notifier 目标不做这种回退。
  const std::string base = PluginBaseId(source_plugin);
  if (base == source_plugin) return false;
  return MatchAnyTarget(rule.compiled->source, base);
}

bool TargetMatches(const Rule& rule, const std::string& source_plugin,
                   const std::string* notifier_id) {
  if (!SourceTargetMatches(rule, source_plugin)) return false;
  if (notifier_id != nullptr) {
    return MatchAnyTarget(rule.compiled->notifier, *notifier_id);
  }
  return rule.compiled->notifier.empty();
}

bool EvalRule(const Rule& rule, RuleFields* fields, EvalContext* context) {
  if (rule.compiled->expr == nullptr) return true;
  const std::array<std::string_view, 4> values{{
      fields->title(),
      fields->content(),
      fields->summary(),
      fields->topic(),
  }};
  return rule.compiled->expr->Eval(values, context);
}

void WarnOnce(std::atomic<bool>* flag, RuleDiagnostics* warnings,
              const std::string& message) {
  if (flag->exchange(true, std::memory_order_relaxed)) return;
  AddDiag(warnings, RuleDiagnostic::Severity::Warn, message);
}

// 在规则自己的 rewrite 之后取快照，且不受同趟后续规则影响。
void AddSaveHit(const Rule& rule, std::size_t index, const RuleFields& fields,
                RuleSaveHits* save_hits) {
  if (save_hits == nullptr || !rule.save.enabled) return;
  RuleSaveHit hit;
  hit.rule_index = index;
  hit.title = std::string(fields.title());
  hit.content = std::string(fields.content());
  hit.summary = std::string(fields.summary());
  hit.level = fields.level();
  save_hits->push_back(std::move(hit));
}

}  // namespace

bool RuleSet::ApplySource(RuleFields* fields, const std::string& source_plugin,
                          RuleDiagnostics* runtime_warnings,
                          RuleSaveHits* save_hits) {
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    Rule& rule = rules_[i];
    Runtime& runtime = *runtime_[i];

    if (rule.delivery_phase) continue;
    if (!runtime.enabled.load(std::memory_order_relaxed)) continue;
    if (!TargetMatches(rule, source_plugin, nullptr)) continue;

    if (rule.type == RuleType::AutoSummary) {
      if (rule.auto_only_if_empty && !fields->summary().empty()) continue;

      // 简介长度由规则决定，缓冲区按正文大小分配；make_summary 的
      // 输出不会长于输入（折叠空白只会变短）。
      const std::string_view content = fields->content();
      std::vector<char> buffer(content.size() + 4);  // 省略号 + NUL
      sm::make_summary(content.data(), rule.auto_max_codepoints, buffer.data(),
                       buffer.size());
      fields->SetSummary(buffer.data());
      runtime.hits.fetch_add(1, std::memory_order_relaxed);
      continue;
    }

    EvalContext context;
    if (!EvalRule(rule, fields, &context)) continue;

    runtime.hits.fetch_add(1, std::memory_order_relaxed);

    if (rule.drop) {
      runtime.blocked.fetch_add(1, std::memory_order_relaxed);
      AddSaveHit(rule, i, *fields, save_hits);
      return false;
    }

    if (rule.has_rewrite()) {
      bool missing_capture = false;
      std::string new_title;
      std::string new_content;
      std::string new_summary;

      if (rule.has_title) {
        new_title = ExpandTemplate(rule.compiled->title, &context,
                                   &missing_capture);
      }
      if (rule.has_content) {
        new_content = ExpandTemplate(rule.compiled->content, &context,
                                     &missing_capture);
      }
      if (rule.has_summary) {
        new_summary = ExpandTemplate(rule.compiled->summary, &context,
                                     &missing_capture);
      }

      if (missing_capture) {
        WarnOnce(&runtime.warned_missing_capture, runtime_warnings,
                 "规则 " + rule.id +
                     " 的模板引用了未参与求值的字段捕获组，替换为空");
      }

      if (rule.has_title) {
        if (new_title.empty()) {
          WarnOnce(&runtime.warned_empty_rewrite, runtime_warnings,
                   "规则 " + rule.id +
                       " 把 title 重写为空，已保留原值");
        } else {
          fields->SetTitle(std::move(new_title));
        }
      }
      if (rule.has_content) {
        if (new_content.empty()) {
          WarnOnce(&runtime.warned_empty_rewrite, runtime_warnings,
                   "规则 " + rule.id +
                       " 把 content 重写为空，已保留原值");
        } else {
          fields->SetContent(std::move(new_content));
        }
      }
      if (rule.has_summary) {
        fields->SetSummary(std::move(new_summary));
      }
      if (rule.has_level) fields->SetLevel(rule.level);
    }

    AddSaveHit(rule, i, *fields, save_hits);
    if (rule.handle) return true;
  }
  return true;
}

bool RuleSet::ApplyDelivery(RuleFields* fields, const std::string& source_plugin,
                            const std::string& notifier_id,
                            RuleDiagnostics* runtime_warnings,
                            RuleSaveHits* save_hits) {
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    Rule& rule = rules_[i];
    Runtime& runtime = *runtime_[i];

    if (!rule.delivery_phase) continue;
    if (!runtime.enabled.load(std::memory_order_relaxed)) continue;
    if (!TargetMatches(rule, source_plugin, &notifier_id)) continue;
    if (rule.type != RuleType::Match) continue;

    EvalContext context;
    if (!EvalRule(rule, fields, &context)) continue;

    runtime.hits.fetch_add(1, std::memory_order_relaxed);

    if (rule.drop) {
      runtime.blocked.fetch_add(1, std::memory_order_relaxed);
      AddSaveHit(rule, i, *fields, save_hits);
      return false;
    }

    if (rule.has_rewrite()) {
      bool missing_capture = false;
      std::string new_title;
      std::string new_content;
      std::string new_summary;

      if (rule.has_title) {
        new_title = ExpandTemplate(rule.compiled->title, &context,
                                   &missing_capture);
      }
      if (rule.has_content) {
        new_content = ExpandTemplate(rule.compiled->content, &context,
                                     &missing_capture);
      }
      if (rule.has_summary) {
        new_summary = ExpandTemplate(rule.compiled->summary, &context,
                                     &missing_capture);
      }

      if (missing_capture) {
        WarnOnce(&runtime.warned_missing_capture, runtime_warnings,
                 "规则 " + rule.id +
                     " 的模板引用了未参与求值的字段捕获组，替换为空");
      }

      if (rule.has_title) {
        if (new_title.empty()) {
          WarnOnce(&runtime.warned_empty_rewrite, runtime_warnings,
                   "规则 " + rule.id +
                       " 把 title 重写为空，已保留原值");
        } else {
          fields->SetTitle(std::move(new_title));
        }
      }
      if (rule.has_content) {
        if (new_content.empty()) {
          WarnOnce(&runtime.warned_empty_rewrite, runtime_warnings,
                   "规则 " + rule.id +
                       " 把 content 重写为空，已保留原值");
        } else {
          fields->SetContent(std::move(new_content));
        }
      }
      if (rule.has_summary) {
        fields->SetSummary(std::move(new_summary));
      }
      if (rule.has_level) fields->SetLevel(rule.level);
    }

    AddSaveHit(rule, i, *fields, save_hits);
    if (rule.handle) return true;
  }
  return true;
}

const char* DefaultRulesFileText() {
  return
      "# SimpleMessage 通知规则\n"
      "#\n"
      "# 规则只在这里手工编写；控制面板的\"规则\"页只负责启用/禁用与查看，\n"
      "# 启用状态保存在 config.toml 的 [rules] 表里。\n"
      "#\n"
      "# 字段说明见 docs/插件开发/03-配置格式.md 的 rules.toml 一节。\n"
      "schema_version = 1\n"
      "\n"
      "# 自动简介：来源没有提供 summary 时，从 content 生成前 20 个码点。\n"
      "# 它默认排在第一位，因此后面的规则可以匹配 summary。删除它就不再自动生成。\n"
      "[[rules]]\n"
      "id = \"auto_summary\"\n"
      "name = \"自动简介\"\n"
      "type = \"auto_summary\"\n"
      "from = \"content\"\n"
      "max_codepoints = 20\n"
      "only_if_empty = true\n";
}

}  // namespace host
}  // namespace sm

// SimpleMessage 测试插件 —— _test.results 的行为基类

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../_test_support.h"

namespace sm_test {

inline bool ParseResultToken(const std::string& token, sm::StartResult* out) {
  if (token == "ok") { *out = sm::StartResult::Ok; return true; }
  if (token == "empty") { *out = sm::StartResult::Empty; return true; }
  if (token == "error") { *out = sm::StartResult::Error; return true; }
  if (token == "abort") { *out = sm::StartResult::Abort; return true; }
  if (token == "cancelled") { *out = sm::StartResult::Cancelled; return true; }
  return false;
}

inline std::vector<sm::StartResult> ParseSequence(const std::string& text) {
  std::vector<sm::StartResult> out;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t comma = text.find(',', begin);
    const std::size_t end = (comma == std::string::npos) ? text.size() : comma;
    std::string token = text.substr(begin, end - begin);
    std::size_t first = token.find_first_not_of(" \t\r\n");
    std::size_t last = token.find_last_not_of(" \t\r\n");
    token = (first == std::string::npos) ? std::string() : token.substr(first, last - first + 1);

    sm::StartResult result = sm::StartResult::Empty;
    if (!token.empty() && ParseResultToken(token, &result)) out.push_back(result);
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  return out;
}

class ResultsBase : public sm::PluginBase {
 public:
  ResultsBase(sm::IHostContext* host, const sm::PluginMeta& meta)
      : PluginBase(host, meta) {}

  bool initialize() override {
    calls_ = 0;
    return true;
  }

  sm::StartResult start(sm::StartOutput* out) override {
    out->items = sm::Span<const sm::Notification>{};
    out->error = nullptr;

    const std::int64_t period_ms = host()->setting_int("period_ms", 500);
    const char* const sequence_setting = host()->setting_string("sequence", "");
    const std::vector<sm::StartResult> sequence =
        ParseSequence(sequence_setting != nullptr ? sequence_setting : "");
    if (sequence.empty()) {
      host()->set_error("sequence 里没有一个可识别的令牌（ok/empty/error/abort/cancelled）");
      host()->schedule_next(period_ms);
      return sm::StartResult::Error;
    }

    const std::size_t index = static_cast<std::size_t>(calls_) % sequence.size();
    const sm::StartResult result = sequence[index];
    ++calls_;
    host()->logf(sm::LogLevel::Info, "第 %lld 次调用 -> 序列第 %zu/%zu 项",
                 static_cast<long long>(calls_), index + 1, sequence.size());

    host()->schedule_next(period_ms);

    switch (result) {
      case sm::StartResult::Ok:
        return ReturnOne(host(), out, "test.results", "第 N 次返回 Ok",
                         "本条说明插件正处于序列里的 ok 位置。");

      case sm::StartResult::Empty:
        return sm::StartResult::Empty;

      case sm::StartResult::Error:
        host()->set_error("按 sequence 返回 Error（用于验证退避与连续失败计数）");
        return sm::StartResult::Error;

      case sm::StartResult::Abort:
        host()->set_error("按 sequence 返回 Abort：本插件应当被立即禁用");
        return sm::StartResult::Abort;

      case sm::StartResult::Cancelled:
        return sm::StartResult::Cancelled;
    }
    return sm::StartResult::Empty;
  }

 private:
  std::int64_t calls_ = 0;
};

}  // namespace sm_test

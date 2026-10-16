// _test.arena_hog —— 循环 allocate() 直到失败，并可选地【不检查】返回值。

#include <cstdio>

#include "../_test_support.h"

namespace {

constexpr sm::SettingDef kSettings[] = {
    sm::setting::boolean("check_result", true, "检查 allocate 的返回值",
                         "关掉后会在 nullptr 上写数据：用于验证崩溃被隔离。"
                         "（T-EXC-17）"),
    sm::setting::integer("chunk_bytes", 4096, 1, 1048576, "每次分配的字节数"),
};

constexpr sm::PluginMeta kMeta = sm_test::SourceMeta(
    "_test.arena_hog", "压力测试：arena",
    "循环分配直到 arena 耗尽，可选择不检查返回值（T-EXC-16/17）。",
    sm::span_of(kSettings));

class ArenaHogPlugin final : public sm::PluginBase {
 public:
  explicit ArenaHogPlugin(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  sm::StartResult start(sm::StartOutput* out) override {
    (void)out;
    const bool check = host()->setting_bool("check_result", true);
    const std::size_t chunk = static_cast<std::size_t>(host()->setting_int("chunk_bytes", 4096));

    std::size_t blocks = 0;
    for (;;) {
      void* const p = host()->allocate(chunk);
      ++blocks;
      if (p == nullptr && check) break;
      static_cast<unsigned char*>(p)[0] = 1;
    }

    char message[160] = {};
    std::snprintf(message, sizeof(message),
                  "arena 上限：分配 %zu 块 × %zu 字节后被拒绝（共 %zu 字节）",
                  blocks, chunk, blocks * chunk);
    host()->log(sm::LogLevel::Warn, message);
    host()->schedule_next(3600 * 1000);
    return sm::StartResult::Empty;
  }
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, ArenaHogPlugin);

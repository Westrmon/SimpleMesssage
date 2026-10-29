// SimpleMessage — 规则命中的通知落盘

#pragma once

#include <mutex>
#include <set>
#include <string>

#include "sm/notification.h"

namespace sm {
namespace host {

struct Rule;

class RuleSaveSink {
 public:
  void SetDataDir(std::string data_dir);
  // path_template 是规则命中时已展开捕获组的 save.path；空串使用默认路径。
  void Save(const Rule& rule, const sm::Notification& view,
            bool delivery_phase, const std::string& notifier_id,
            const std::string& path_template);

 private:
  bool ShouldWarnLocked(const std::string& path);

  std::mutex           mutex_;
  std::string          data_dir_;
  std::set<std::string> warned_paths_;
};

}  // namespace host
}  // namespace sm

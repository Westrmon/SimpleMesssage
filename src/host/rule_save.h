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
  void Save(const Rule& rule, const sm::Notification& view,
            bool delivery_phase, const std::string& notifier_id);

 private:
  bool ShouldWarnLocked(const std::string& path);

  std::mutex           mutex_;
  std::string          data_dir_;
  std::set<std::string> warned_paths_;
};

}  // namespace host
}  // namespace sm

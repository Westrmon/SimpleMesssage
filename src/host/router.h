// SimpleMessage — 通知路由：topic 模式匹配与订阅解析
#pragma once

#include <string>
#include <vector>

#include "sm/types.h"

#include "host/config.h"

namespace sm {
namespace host {

struct Subscriber {
  std::string              id;
  std::vector<std::string> patterns;
  bool                     all_topics = false;
};

struct ResolvedSubscription {
  std::vector<std::string> patterns;
  bool                     all_topics = false;
};

struct NotifierSubscriptions {
  std::string           id;
  sm::Span<const char*> declared{};
};

ResolvedSubscription ResolveSubscriber(
    sm::Span<const char*> declared,
    const std::vector<std::string>* override_patterns);

bool TopicMatches(const char* pattern, const std::string& topic);

std::vector<Subscriber> BuildSubscribers(
    const std::vector<NotifierSubscriptions>& declared,
    const config::RoutingMap& routing);

class Router {
 public:
  Router() = default;
  Router(const Router&) = delete;
  Router& operator=(const Router&) = delete;

  void Rebuild(std::vector<Subscriber> subscribers);
  void Clear();

  void Match(const std::string& topic, std::vector<std::string>* out) const;

  const std::vector<Subscriber>& subscribers() const { return subscribers_; }
  bool empty() const { return subscribers_.empty(); }

 private:
  std::vector<Subscriber> subscribers_;
};

}  // namespace host
}  // namespace sm

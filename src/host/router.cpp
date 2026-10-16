// SimpleMessage — 通知路由的实现
#include "host/router.h"

#include <algorithm>
#include <cstring>

namespace sm {
namespace host {

ResolvedSubscription ResolveSubscriber(
    sm::Span<const char*> declared,
    const std::vector<std::string>* override_patterns) {
  ResolvedSubscription out;

  if (override_patterns != nullptr) {
    out.patterns = *override_patterns;
    return out;
  }

  if (declared.data == nullptr) {
    out.all_topics = true;
    return out;
  }
  out.patterns.reserve(declared.size);
  for (std::size_t i = 0; i < declared.size; ++i) {
    const char* const pattern = declared.data[i];
    if (pattern == nullptr || pattern[0] == '\0') continue;
    out.patterns.emplace_back(pattern);
  }
  out.all_topics = out.patterns.empty();
  return out;
}

bool TopicMatches(const char* pattern, const std::string& topic) {
  if (pattern == nullptr) return false;
  const std::size_t length = std::strlen(pattern);
  if (length == 0) return false;

  if (length >= 2 && pattern[length - 1] == '*' && pattern[length - 2] == '.') {
    const std::size_t prefix = length - 2;
    return topic.size() > prefix && topic[prefix] == '.' &&
           topic.compare(0, prefix, pattern, prefix) == 0;
  }

  return topic.size() == length && topic.compare(0, length, pattern, length) == 0;
}

std::vector<Subscriber> BuildSubscribers(
    const std::vector<NotifierSubscriptions>& declared,
    const config::RoutingMap& routing) {
  std::vector<Subscriber> out;
  out.reserve(declared.size());
  for (const NotifierSubscriptions& decl : declared) {
    Subscriber subscriber;
    subscriber.id = decl.id;
    const auto it = routing.find(decl.id);
    ResolvedSubscription resolved = ResolveSubscriber(
        decl.declared, it == routing.end() ? nullptr : &it->second);
    subscriber.patterns = std::move(resolved.patterns);
    subscriber.all_topics = resolved.all_topics;
    out.push_back(std::move(subscriber));
  }
  std::sort(out.begin(), out.end(),
            [](const Subscriber& a, const Subscriber& b) { return a.id < b.id; });
  return out;
}

void Router::Rebuild(std::vector<Subscriber> subscribers) {
  subscribers_ = std::move(subscribers);
}

void Router::Clear() { subscribers_.clear(); }

void Router::Match(const std::string& topic, std::vector<std::string>* out) const {
  if (out == nullptr) return;
  for (const Subscriber& subscriber : subscribers_) {
    if (subscriber.all_topics) {
      out->push_back(subscriber.id);
      continue;
    }
    for (const std::string& pattern : subscriber.patterns) {
      if (TopicMatches(pattern.c_str(), topic)) {
        out->push_back(subscriber.id);
        break;
      }
    }
  }
}

}  // namespace host
}  // namespace sm

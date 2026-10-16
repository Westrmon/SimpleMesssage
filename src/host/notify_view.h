// SimpleMessage — 通知的呈现：过滤、列表、详情与导出
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sm/host_context.h"
#include "sm/notification.h"

#include "host/action_dispatch.h"
#include "host/log.h"
#include "host/notification_pipeline.h"
#include "host/router.h"
#include "host/ui_state.h"

namespace sm {
namespace host {

std::uint32_t LevelColor(sm::Level level);
std::uint32_t LogLevelColor(sm::LogLevel level);

bool MatchesNotifyFilter(const NotifyFilter& filter, const OwnedNotification& n);
bool MatchesLogFilter(const LogFilter& filter, const log::Entry& entry);

bool TopicHasSubscribers(const Router* router, const std::string& topic);

std::string NotificationJsonLine(const OwnedNotification& n);

bool WriteNotificationsJsonl(const std::string& utf8_path,
                             const std::vector<const OwnedNotification*>& items,
                             std::string* out_error);

std::int64_t DrawNotificationList(const char* table_id,
                                  const std::vector<const OwnedNotification*>& items,
                                  std::int64_t selected_seq);

bool DrawNotificationDetail(const char* id_scope, const OwnedNotification& n,
                            const Router* router, IActionDispatcher* dispatcher);

}  // namespace host
}  // namespace sm

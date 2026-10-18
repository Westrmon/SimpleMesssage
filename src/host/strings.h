// SimpleMessage — 界面文案
#pragma once

#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"

namespace sm {
namespace host {
namespace strings {

inline constexpr const char* kConfirm = "确定";
inline constexpr const char* kCancel = "取消";
inline constexpr const char* kRetry = "重试";
inline constexpr const char* kOpen = "打开";
inline constexpr const char* kSettingsReset = "已恢复这个插件的默认设置。";
inline constexpr const char* kLabelValueFormat = "%s：%s";

inline constexpr const char* kWindowTitleFormat = "SimpleMessage %s";
inline constexpr const char* kNotifyWindowTitle = "SimpleMessage 通知";

inline constexpr const char* kPageOverview = "概览";
inline constexpr const char* kPagePlugins = "插件";
inline constexpr const char* kPageNotifications = "通知";
inline constexpr const char* kPageRules = "规则";
inline constexpr const char* kPageRouting = "路由";
inline constexpr const char* kPageLog = "日志";
inline constexpr const char* kPageAbout = "关于";

inline constexpr const char* kSafeModeBanner = "安全模式：未加载任何插件，也不会写回配置。";
inline constexpr const char* kSafeModeHint = "以 --safe-mode 启动。确认配置修好后重新启动即可恢复正常。";
inline constexpr const char* kConfigSaveFailedBanner = "配置未能保存到磁盘，改动目前只存在于内存中。";
inline constexpr const char* kConfigSaveFailedHint =
    "常见原因是目录不可写或磁盘已满。重试仍失败时，退出前请手工备份界面上的改动。";

inline constexpr const char* kSecVersion = "版本";
inline constexpr const char* kSecRuntime = "运行状态";
inline constexpr const char* kSecPluginStats = "插件统计";
inline constexpr const char* kSecNotifyStats = "通知统计";
inline constexpr const char* kSecErrors = "错误汇总";

inline constexpr const char* kHostVersionFormat = "宿主 %s（API %d）";
inline constexpr const char* kUptimeFormat = "已运行 %s";
inline constexpr const char* kSingleInstanceOwner = "单实例：本进程是所有者";
inline constexpr const char* kSingleInstanceShared = "单实例：未启用（允许多开）";
inline constexpr const char* kSchedulerPaused = "调度：已被“暂停调度”暂停";
inline constexpr const char* kSchedulerRunning = "调度：正常";
inline constexpr const char* kSchedulerStuckFormat = "调度：正卡在 %s（已连续超时 %d 次）";

inline constexpr const char* kStatLoadedFormat = "已加载 %d";
inline constexpr const char* kStatEnabledFormat = "已启用 %d";
inline constexpr const char* kStatDisabledFormat = "已禁用（自动） %d";
inline constexpr const char* kStatRejectedFormat = "被拒绝 %d";
inline constexpr const char* kStatProducedFormat = "本次运行收到 %lld 条";
inline constexpr const char* kStatQueuedFormat = "当前队列 %lld 条";
inline constexpr const char* kStatDroppedFormat = "已被丢弃 %lld 条";
inline constexpr const char* kStatUndeliveredFormat = "未被投递 %lld 条";
inline constexpr const char* kStatTaskQueueFormat = "主线程任务队列 %d 条";
inline constexpr const char* kDroppedHint =
    "队列满时丢弃最旧的通知。这条计数不为 0 说明界面或插件跟不上通知的产生速度。";
inline constexpr const char* kUndeliveredHint =
    "没有任何 Notifier 收到它们 —— 通常是路由页里某个 topic 后面没有订阅者。";
inline constexpr const char* kNoErrors =
    "本次运行没有插件崩溃、初始化失败、被自动禁用或执行失败。";
inline constexpr const char* kLastCrashFormat = "最近一次崩溃：%s（%s）";
inline constexpr const char* kLastInitFailedFormat = "最近一次初始化失败：%s（%s）";
inline constexpr const char* kLastAutoDisabledFormat = "已被自动禁用：%s（%s）";
inline constexpr const char* kLastStartFailedFormat =
    "执行失败、正在重试：%s（连续 %d 次，%s）";
inline constexpr const char* kRetryInFormat = "%s后重试";
inline constexpr const char* kStartFailedNoRetry = "没有登记下一次重试";
inline constexpr const char* kGoToPlugin = "跳到插件页";

inline constexpr const char* kColEnabled = "启用";
inline constexpr const char* kColName = "名称";
inline constexpr const char* kColId = "id";
inline constexpr const char* kColKind = "类型";
inline constexpr const char* kColVersion = "版本";
inline constexpr const char* kColState = "状态";
inline constexpr const char* kColActivity = "最近活动";
inline constexpr const char* kKindSource = "调度子项";
inline constexpr const char* kKindNotifier = "通知器";

inline constexpr const char* kReasonFormat = "原因：%s";
inline constexpr const char* kHostRangeFormat = "要求的宿主版本：%s（%s）";
inline constexpr const char* kHostRangeOk = "满足";
inline constexpr const char* kHostRangeBad = "不满足";
inline constexpr const char* kAuthorFormat = "作者：%s";
inline constexpr const char* kConfigPathFormat = "配置：%s";
inline constexpr const char* kReEnable = "重新启用";
inline constexpr const char* kReEnableHint =
    "会写回 [[instances]] 并立即重新加载它的模块；它自己的设置项不受影响。";
inline constexpr const char* kResetSettings = "重置为默认值";

// ---- 插件实例 ----
inline constexpr const char* kAddInstance = "添加实例";
inline constexpr const char* kAddInstanceTitle = "添加插件实例";
inline constexpr const char* kAddInstanceTypeLabel = "插件类型";
inline constexpr const char* kAddInstanceNameLabel = "实例名称（留空则自动命名）";
inline constexpr const char* kAddInstanceNameHint =
    "名称只影响界面显示；实例 id 与设置文件由宿主生成，之后不可更改。";
inline constexpr const char* kAddInstanceCountFormat = "%d 个实例";
inline constexpr const char* kAddInstanceNoTypes =
    "没有可用的通知源插件。插件 DLL 必须已通过校验，且实例数未达上限。";
inline constexpr const char* kAddInstanceReadonly = "配置当前为只读，不能添加实例。";
inline constexpr const char* kAddInstanceFailedFormat = "添加失败：%s";
inline constexpr const char* kAddInstanceOkFormat = "已添加实例 %s。";
inline constexpr const char* kInstanceTypeFormat = "插件类型：%s（%s）";
inline constexpr const char* kInstanceIdFormat = "实例 id：%s";
inline constexpr const char* kInstanceOrdinalFormat = "实例序号：%d / %d";
inline constexpr const char* kInstanceNameLabel = "实例名称";
inline constexpr const char* kInstanceNameHint =
    "改名只影响界面显示；实例 id 与设置文件不会改变。";
inline constexpr const char* kInstanceNone = "这个插件类型当前没有实例。";
inline constexpr const char* kInstanceAddForType = "为此类型添加实例";
inline constexpr const char* kInstanceDelete = "删除实例";
inline constexpr const char* kInstanceDeleteTitle = "删除这个实例？";
inline constexpr const char* kInstanceDeleteBodyFormat =
    "将删除实例「%s」（%s）。\n"
    "设置文件 %s 会被删除且无法恢复；已经产生的通知历史不受影响。";
inline constexpr const char* kInstanceDeletedFormat = "已删除实例 %s。";
inline constexpr const char* kInstanceDeleteFailedFormat = "删除失败：%s";
inline constexpr const char* kInstanceReadonlyHint =
    "配置当前为只读：添加、删除、改名与启用/禁用都不会写回 config.toml。";
inline constexpr const char* kInstanceRejectedHint =
    "这个类型没有通过校验，不能添加实例。请查看上面的原因与日志。";
inline constexpr const char* kInstanceTypeRowHint =
    "这是插件类型行，不是运行实例。它没有设置文件。";

inline constexpr const char* kActivityNone = "（还没有活动）";
inline constexpr const char* kActivityStartFormat = "start() %s · %s";
inline constexpr const char* kActivityNotifyFormat = "on_notify() %s · %s";
inline constexpr const char* kActivityCrashed = "崩溃";
inline constexpr const char* kActivityStarted = "已启动";

inline constexpr const char* kStateNotLoaded = "未加载";
inline constexpr const char* kStateLoaded = "已加载";
inline constexpr const char* kStateInitialized = "已初始化";
inline constexpr const char* kStateRunning = "运行中";
inline constexpr const char* kStateStopped = "已停止";
inline constexpr const char* kStateUnloaded = "已卸载";
inline constexpr const char* kStateDisabledFormat = "已禁用（%s）";
inline constexpr const char* kStateInitFailedFormat = "初始化失败（%s）";
inline constexpr const char* kStateRejectedFormat = "已拒绝加载（%s）";
inline constexpr const char* kStateNoReason = "原因未记录";

inline constexpr const char* kSecSettings = "设置";

inline constexpr const char* kBulkEnableAll = "全部启用";
inline constexpr const char* kBulkDisableAll = "全部禁用";
inline constexpr const char* kNoPlugins = "没有发现任何插件。插件目录里放着 .dll 才会出现在这里。";

inline constexpr const char* kSettingRestartSuffix = "（重启后生效）";
inline constexpr const char* kNoSettings = "这个插件没有声明任何设置项。";
inline constexpr const char* kResetSettingsTitle = "重置为默认值？";
inline constexpr const char* kResetSettingsBodyFormat =
    "会删掉 %s 的设置文件，并把它声明的每一项恢复为默认值。\n"
    "插件下次读取设置时立即拿到默认值；这一项无法撤销。";
inline constexpr const char* kSettingDirtyHint =
    "改动立即对插件生效，写盘有防抖 —— 不需要“应用”按钮。";
inline constexpr const char* kSettingsUnavailableFormat =
    "插件当前没有加载（%s），设置表单要等它重新启用之后才能生成。";
inline constexpr const char* kSettingSchemaTooNew =
    "这个插件的设置文件版本高于本宿主，界面不会写回它 —— 手工编辑也请谨慎。";
inline constexpr const char* kSettingBrowse = "浏览…";
inline constexpr const char* kSettingPreview = "试听";
inline constexpr const char* kSettingSoundFilter = "WAV / MP3 文件 (*.wav;*.mp3)";
inline constexpr const char* kSettingAllFilesFilter = "所有文件 (*.*)";
inline constexpr const char* kSettingPreviewFailedTitle = "无法试听";

inline constexpr const char* kColTime = "时间";
inline constexpr const char* kColLevel = "级别";
inline constexpr const char* kColSource = "来源";
inline constexpr const char* kColTopic = "topic";
inline constexpr const char* kColTitle = "标题";
inline constexpr const char* kColSummary = "摘要";

inline constexpr const char* kFilterLevel = "级别";
inline constexpr const char* kFilterSource = "来源";
inline constexpr const char* kFilterTopic = "topic 前缀";
inline constexpr const char* kFilterText = "文本";
inline constexpr const char* kFilterSourceAll = "全部";
inline constexpr const char* kFilterClear = "清除过滤";
inline constexpr const char* kFilteredFormat = "筛选出 %d / %d 条";

inline constexpr const char* kSecNotifySurface = "通知表面";
inline constexpr const char* kNotifyMonitor = "显示器";
inline constexpr const char* kNotifyMonitorPrimary = "主屏（primary）";
inline constexpr const char* kNotifyMonitorFormat = "%s（%d,%d %dx%d）";
inline constexpr const char* kNotifyMonitorMissingFormat = "（已断开）%s";
inline constexpr const char* kNotifyPosition = "位置";
inline constexpr const char* kNotifyCornerTopLeft = "左上";
inline constexpr const char* kNotifyCornerTopRight = "右上";
inline constexpr const char* kNotifyCornerBottomLeft = "左下";
inline constexpr const char* kNotifyCornerBottomRight = "右下";
inline constexpr const char* kNotifySurfaceHint =
    "浮层出现的那扇窗口贴在这块屏幕的这个角上，改动立即生效；这里写回的就是 "
    "config.toml 的 [notify_window]。";
inline constexpr const char* kNotifyMonitorMissingHint =
    "配置里的这块屏幕现在不在系统里 —— 通知会临时弹在主屏上，插回显示器即恢复。";

inline constexpr const char* kSecRuleSave = "规则保存";
inline constexpr const char* kAllowRuleSave = "允许规则保存通知内容";
inline constexpr const char* kAllowRuleSaveHint =
    "关闭时所有规则的 save 都不生效；开启后无视通知来源，命中的规则会立即把"
    "通知追加写入文件。配置写回 config.toml 的 [app] allow_rule_save。";

inline constexpr const char* kHistoryEmpty = "内存历史里还没有通知。";
inline constexpr const char* kHistoryCleared = "历史已清空（seq 不重置）。";

inline constexpr const char* kExportFiltered = "导出当前筛选结果…";
inline constexpr const char* kExportAll = "导出全部历史…";
inline constexpr const char* kExportOkFormat = "已导出 %d 条到 %s";
inline constexpr const char* kExportFailedFormat = "导出失败（%s）：%s";
inline constexpr const char* kClearHistory = "清空历史";
inline constexpr const char* kClearHistoryTitle = "清空通知历史？";
inline constexpr const char* kClearHistoryBody =
    "只清掉内存里的最近通知，已经导出的文件不受影响，之后新到的通知照常显示。\n"
    "通知的 seq 不会重置 —— 重置会让旧导出文件里的 seq 与新通知撞车。";

inline constexpr const char* kDetailContent = "完整内容";
inline constexpr const char* kDetailActions = "动作";
inline constexpr const char* kDetailNoAction = "这条通知没有动作。";
inline constexpr const char* kDetailUnrouted =
    "没有 Notifier 订阅这个 topic，所以点动作没有任何插件会收到（见路由页）。";
inline constexpr const char* kSelectRowHint = "点列表里的一行看详情。";

// ---- 规则页 ----
inline constexpr const char* kRulesReadonly =
    "规则内容只能写在 rules.toml 里；这一页只负责启用/禁用与查看，"
    "启用状态写回 config.toml 的 [rules]。";
inline constexpr const char* kRulesReload = "重新加载规则";
inline constexpr const char* kRulesFileFormat = "规则文件：%s";
inline constexpr const char* kRulesReloadOkFormat = "已重新加载 %d 条规则。";
inline constexpr const char* kRulesReloadFailed =
    "重新加载失败，继续使用上一次的规则集（原因见日志）。";
inline constexpr const char* kRulesNone = "当前没有规则：rules.toml 为空，或所有规则都被跳过。";
inline constexpr const char* kRulesSkippedFormat =
    "有 %d 条规则因配置错误被跳过，详情见启动日志。";
inline constexpr const char* kRulesCountFormat = "共 %d 条规则";
inline constexpr const char* kRulesSafeModeHint =
    "安全模式下不能改动启用状态：本次不会写回 config.toml。";
inline constexpr const char* kColRuleEnabled = "启用";
inline constexpr const char* kColRuleIndex = "#";
inline constexpr const char* kColRuleHits = "命中 / 拦截";
inline constexpr const char* kColRuleType = "类型";
inline constexpr const char* kColRulePhase = "阶段";
inline constexpr const char* kColRuleTarget = "目标";
inline constexpr const char* kColRuleDetail = "详情";
inline constexpr const char* kRuleTypeMatch = "match";
inline constexpr const char* kRuleTypeAutoSummary = "auto_summary";
inline constexpr const char* kRulePhaseSource = "来源";
inline constexpr const char* kRulePhaseDelivery = "投递";
inline constexpr const char* kRuleTargetGlobal = "全局来源";
inline constexpr const char* kRuleTargetSourceFormat = "source：%s";
inline constexpr const char* kRuleTargetNotifierFormat = "notifier：%s";
inline constexpr const char* kRuleStatusEnabled = "启用";
inline constexpr const char* kRuleStatusDisabled = "禁用";
inline constexpr const char* kRuleIdFormat = "id：%s";
inline constexpr const char* kRuleNameFormat = "名称：%s";
inline constexpr const char* kRuleCaseInsensitive = "忽略大小写";
inline constexpr const char* kRuleHandle = "匹配后停止（handle = true）";
inline constexpr const char* kRuleDrop = "丢弃（drop = true）";
inline constexpr const char* kRuleFromFormat = "简介来源：%s";
inline constexpr const char* kRuleMaxFormat = "简介最多 %zu 个码点";
inline constexpr const char* kRuleOnlyIfEmpty = "仅当简介为空时生成";
inline constexpr const char* kRuleNoMatch = "（无条件匹配）";
inline constexpr const char* kRuleNoRewrite = "（不重写）";
inline constexpr const char* kRuleNoTarget = "（不限通知源）";
inline constexpr const char* kRuleRewriteTitleFormat = "title = %s";
inline constexpr const char* kRuleRewriteContentFormat = "content = %s";
inline constexpr const char* kRuleRewriteSummaryFormat = "summary = %s";
inline constexpr const char* kRuleRewriteLevelFormat = "level = %s";
inline constexpr const char* kRuleSaveEnabled = "保存：已启用";
inline constexpr const char* kRuleSaveDisabled = "保存：未启用";
inline constexpr const char* kRuleSaveDefaultPath =
    "保存路径：默认（notify/{ruleid}.txt，相对 data_dir）";
inline constexpr const char* kRuleSavePathFormat = "保存路径：%s";
inline constexpr const char* kRuleSaveGlobalOff =
    "通知页尚未允许规则保存，当前不会写入。";

inline constexpr const char* kRoutingTable1 = "Notifier 与它的有效订阅";
inline constexpr const char* kRoutingTable2 = "最近的 topic 与谁会收到它";
inline constexpr const char* kRoutingFromMeta = "元信息";
inline constexpr const char* kRoutingFromConfig = "配置覆盖";
inline constexpr const char* kRoutingAllTopics = "（订阅全部）";
inline constexpr const char* kRoutingNoneTopics = "（一条都不收）";
inline constexpr const char* kRoutingNoSubscribers = "（没有 Notifier 会收到它）";
inline constexpr const char* kRoutingUnroutedFormat = "无订阅者 %lld 条";
inline constexpr const char* kRoutingNoTopic = "内存历史里还没有出现过 topic。";
inline constexpr const char* kRoutingNoNotifier = "当前没有已加载的 Notifier。";
inline constexpr const char* kRoutingFooterFormat =
    "路由由 config.toml 的 [routing] 与插件的元信息共同决定。要修改请编辑：%s";
inline constexpr const char* kRoutingReadonly = "这一页只读：界面只负责让人看懂合并结果，"
                                                "决定的唯一地方是配置文件。";
inline constexpr const char* kColNotifier = "Notifier";
inline constexpr const char* kColPatterns = "订阅模式";
inline constexpr const char* kColReceivers = "会收到它的 Notifier";
inline constexpr const char* kColMessage = "消息";

inline constexpr const char* kLogFollow = "跟随最新";
inline constexpr const char* kLogPause = "暂停取新快照";
inline constexpr const char* kLogOpenFile = "打开日志文件";
inline constexpr const char* kLogOpenDir = "打开日志目录";
inline constexpr const char* kLogSearch = "搜索";
inline constexpr const char* kLogNoFile = "日志目录还没准备好（不可写？），现在没有可打开的文件。";
inline constexpr const char* kLogDroppedFormat =
    "内存缓冲只保留最近 1024 条，更早的 %llu 条已从这里滚出去。";
inline constexpr const char* kLogPausedHint = "已暂停：缓冲里新来的记录仍会写盘，只是不再进这一页。";
inline constexpr const char* kLogEmpty = "还没有日志。";
inline constexpr const char* kNoMatch = "没有符合过滤条件的记录。";

inline constexpr const char* kSecBuild = "构建";
inline constexpr const char* kSecPaths = "路径";
inline constexpr const char* kSecInterface = "界面";
inline constexpr const char* kSecLicense = "许可";
inline constexpr const char* kSecThirdParty = "第三方依赖";
inline constexpr const char* kSecExit = "退出";
inline constexpr const char* kNone = "无";
inline constexpr const char* kPathConfigDir = "配置目录";
inline constexpr const char* kPathDataDir = "数据目录";
inline constexpr const char* kPathPluginDir = "插件目录";
inline constexpr const char* kPathLogFile = "当前日志文件";
inline constexpr const char* kPathUiState = "界面状态文件";
inline constexpr const char* kExitButton = "退出 SimpleMessage";
inline constexpr const char* kExitHint = "走完整的关停顺序：停调度、停插件、卸载、写日志、退出。";

inline constexpr const char* kBuildVersion = "版本";
inline constexpr const char* kBuildDetail = "内部版本";
inline constexpr const char* kBuildApi = "API 版本";
inline constexpr const char* kBuildType = "构建类型";
inline constexpr const char* kBuildCompiler = "编译器";
inline constexpr const char* kBuildPlatform = "目标平台";
inline constexpr const char* kLicensePending = "未定 —— 本仓库尚未附上许可证（见 README 的许可一节）";
inline constexpr const char* kLicenseHint =
    "第三方依赖各自的许可证见下表；它们的全文随依赖一起放在 external/ 下。";

inline constexpr const char* kFontSize = "界面字号";
inline constexpr const char* kFontSizeHint =
    "改动立即重建字体图集（会有一帧的停顿），并写回 config.toml 的 [app] ui_font_size。";
inline constexpr const char* kFontFileFormat = "当前字体：%s";
inline constexpr const char* kFontFallbackBuiltin =
    "（内置字体）系统里几个常见中文字体都没找到，界面上的中文可能显示成问号。";

inline constexpr const char* kHostStartedDetail = "SimpleMessage 已启动，插件与调度开始工作。";
inline constexpr const char* kHostExitedDetail = "SimpleMessage 已退出本次运行。";
inline constexpr const char* kConfigResetDetail =
    "config.toml 读不出来或解析失败，原文件已改名备份，本次按默认值启动 —— 之前手工"
    "改过的设置需要重新设置。";
inline constexpr const char* kPluginSchemaTooNewDetail =
    "有插件的设置文件版本高于本宿主。为避免把它的内容覆盖成本宿主认识的格式，"
    "宿主拒绝启动（退出码 2）。";

inline constexpr const char* kTrayTooltip = "SimpleMessage —— 通知聚合与投递";
inline constexpr const char* kTrayOpenPanel = "打开控制面板";
inline constexpr const char* kTrayPause = "暂停调度";
inline constexpr const char* kTrayOpenConfigDir = "打开配置目录";
inline constexpr const char* kTrayOpenLogDir = "打开日志目录";
inline constexpr const char* kTrayAbout = "关于";
inline constexpr const char* kTrayExit = "退出";

inline constexpr const char* kCloseHintTitle = "SimpleMessage 仍在后台运行";
inline constexpr const char* kCloseHintText =
    "面板已关闭，程序继续在托盘里运行。要重新打开，单击托盘图标即可。";
inline constexpr const char* kNoTrayHint =
    "托盘图标创建失败（资源管理器未运行？）。面板里的“退出 SimpleMessage”仍然可用。";

inline constexpr const char* kUsageText =
    "SimpleMessage —— 基于插件机制的通知聚合与投递工具\n"
    "\n"
    "用法：sm_host.exe [选项]\n"
    "\n"
    "选项：\n"
    "  --config-dir <路径>   覆盖配置目录（默认 <exe>\\config，不可写时用 %APPDATA%\\SimpleMessage\\config）\n"
    "  --data-dir <路径>     覆盖数据目录（日志与界面状态）\n"
    "  --plugin-dir <路径>   覆盖插件目录（默认 <exe>\\plugins）\n"
    "  --console             跟随父进程的控制台，日志同时输出到标准输出\n"
    "  --safe-mode           不加载任何插件、不写回配置，直接打开控制面板\n"
    "  --renderer <名称>     渲染后端：auto（默认，优先 DX11，不可用则 OpenGL）、\n"
    "                        dx11、opengl\n"
    "  --help, -h            显示本帮助后退出\n"
    "  --version             显示版本后退出\n"
    "\n"
    "未知参数只会打印一行提示，不会拒绝启动。\n";

inline constexpr const char* kUnknownArgFormat = "未知参数（已忽略）：%s\n";
inline constexpr const char* kMissingValueFormat = "参数 %s 缺少取值（已忽略）。\n";

inline constexpr const char* kUnknownRendererFormat =
    "无法识别的渲染器名称（已按 auto 处理）：%s\n";

inline constexpr const char* LevelText(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return "调试";
    case sm::Level::Info:     return "信息";
    case sm::Level::Warning:  return "警告";
    case sm::Level::Error:    return "错误";
    case sm::Level::Critical: return "严重";
  }
  return "信息";
}

inline constexpr const char* LogLevelText(sm::LogLevel level) {
  switch (level) {
    case sm::LogLevel::Trace: return "跟踪";
    case sm::LogLevel::Debug: return "调试";
    case sm::LogLevel::Info:  return "信息";
    case sm::LogLevel::Warn:  return "警告";
    case sm::LogLevel::Error: return "错误";
  }
  return "信息";
}

inline constexpr const char* StartResultText(sm::StartResult result) {
  switch (result) {
    case sm::StartResult::Ok:
      return "返回了通知";
    case sm::StartResult::Empty:
      return "没有新数据";
    case sm::StartResult::Error:
      return "报错";
    case sm::StartResult::Abort:
      return "放弃后续调度";
    case sm::StartResult::Cancelled:
      return "被取消";
  }
  return "未知结果";
}

}  // namespace strings
}  // namespace host
}  // namespace sm

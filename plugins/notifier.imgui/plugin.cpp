// SimpleMessage 示例插件 —— notifier.imgui

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <imgui.h>

#include "sm/export.h"
#include "sm/host_context.h"
#include "sm/notification.h"
#include "sm/plugin.h"

namespace {

using sm::PluginBase;
using sm::PluginKind;
using sm::PluginMeta;
using sm::SettingDef;
using sm::StartOutput;
using sm::StartResult;

constexpr SettingDef kSettings[] = {
    sm::setting::integer("toast_duration_sec", 5, 0, 120, "浮层驻留时间（秒）",
                         "浮层出现后自动消失的时间。设为 0 时不会自动消失。"),
    sm::setting::integer("max_toasts", 3, 1, 10, "同时显示的最大浮层数",
                         "超出的通知只在面板的历史里出现，不弹浮层。"),
    sm::setting::integer("toast_width_min", 320, 200, 1200, "浮层最小宽度（像素）",
                         "浮层本身是宿主按内容撑开的一扇窗口，这里约束的是【内容】"
                         "的宽度。比 toast_width_max 大时以 max 为准。"),
    sm::setting::integer("toast_width_max", 460, 240, 2000, "浮层最大宽度（像素）",
                         "长摘要在这个宽度内换行，而不是把浮层撑宽顶出屏幕。"),
    sm::setting::boolean("show_actions", true, "在浮层上显示 Action 按钮",
                         "关掉后 Action 只在详情视图里出现。"),
    sm::setting::boolean("show_details_button", true, "在浮层上显示「详情」按钮",
                         "点它打开详情视图，里面是通知的完整 content —— 浮层上"
                         "只放得下摘要，完整内容要有这条入口。"),
    sm::setting::boolean("show_read_button", true, "在浮层上显示「已读」按钮",
                         "等价于在浮层上点右键：收起这张浮层，并把宿主历史里的"
                         "那条标记为已读。"),
};

// 驻留时间为 0 的浮层是常驻的：宿主只按"最长引用"记账，所以在它还在屏幕上
// 的每一帧都续租一小段，而不是用 request_ui_surface(0) —— 后者是不可自然到期
// 的永久引用，只有 close_view() 能归还，会把通知表面钉死到插件卸载。
constexpr std::int64_t kNeverExpiresMs =
    std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kPersistKeepAliveMs = 5 * 1000;

constexpr PluginMeta kMeta = {
     "notifier.imgui",
     "imgui 通知浮层",
     "SimpleMessage 示例",
     "1.0.0",
     "在屏幕角落弹出 imgui 通知浮层，支持点击展开详情。",
     PluginKind::Notifier,
     ">=0.1.0,<1.0.0",
     sm::Span<const char*>{},
     sm::span_of(kSettings),
     SM_API_VERSION,
};

const char* LevelText(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return "调试";
    case sm::Level::Info:     return "信息";
    case sm::Level::Warning:  return "警告";
    case sm::Level::Error:    return "错误";
    case sm::Level::Critical: return "严重";
  }
  return "信息";
}

ImVec4 LevelColor(sm::Level level) {
  switch (level) {
    case sm::Level::Debug:    return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
    case sm::Level::Info:     return ImVec4(0.35f, 0.78f, 1.00f, 1.0f);
    case sm::Level::Warning:  return ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
    case sm::Level::Error:    return ImVec4(1.00f, 0.45f, 0.35f, 1.0f);
    case sm::Level::Critical: return ImVec4(1.00f, 0.30f, 0.40f, 1.0f);
  }
  return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

std::string AgoText(std::int64_t now_ms, std::int64_t then_ms) {
  const std::int64_t secs = (now_ms - then_ms) / 1000;
  if (secs < 5) return "刚刚";
  if (secs < 60) return std::to_string(secs) + " 秒前";
  const std::int64_t mins = secs / 60;
  if (mins < 60) return std::to_string(mins) + " 分钟前";
  const std::int64_t hours = mins / 60;
  if (hours < 24) return std::to_string(hours) + " 小时前";
  return std::to_string(hours / 24) + " 天前";
}

class ImGuiNotifier final : public PluginBase {
 public:
  explicit ImGuiNotifier(sm::IHostContext* host) : PluginBase(host, kMeta) {}

  bool initialize() override {
    volatile const void* probe = reinterpret_cast<const void*>(&ImGui::NewFrame);
    (void)probe;

    return true;
  }

  void stop() override {}

  void exit() override {
    toasts_.clear();
    detail_ = Detail{};

  }

  void on_notify(const sm::Notification& n) override {
    if (n.seq <= last_seq_) return;
    last_seq_ = n.seq;

    const std::int64_t hold_ms = DurationMs();
    Toast toast;
    toast.notice = CopyNotice(n);
    toast.expire_unix_ms =
        (hold_ms > 0) ? (host()->now_unix_ms() + hold_ms) : kNeverExpiresMs;

    toasts_.push_back(std::move(toast));
    TrimToLimit();

    // 常驻浮层（0 秒）没有到期时刻：先让宿主把通知表面建起来，
    // 之后由 draw_ui() 在它还在屏幕上时逐帧续租。
    host()->request_ui_surface(hold_ms > 0 ? hold_ms : kPersistKeepAliveMs);
  }

  void on_action(const sm::Notification& n, const char* action_id) override {
    if (action_id == nullptr) return;

    if (std::strcmp(action_id, "dismiss") == 0) {
      RemoveToast(n.seq);
      return;
    }

    if (std::strcmp(action_id, "details") == 0) {
      detail_.notice = CopyNotice(n);
      detail_.open = true;
      RemoveToast(n.seq);

      const std::string title = detail_.notice.title + " — 详情";
      host()->set_view_title(title.c_str());
      // 详情窗口由 set_view_title() 的 [视图] 引用维持，不要再请求
      // request_ui_surface(0)：那会把通知表面钉成永久引用，而且 close_view()
      // 会把它连同常驻浮层的续租一起清掉。
      return;
    }

  }

  void draw_ui(sm::Span<const sm::Notification> history) override {
    (void)history;

    if (host()->imgui_context() == nullptr) return;

    const std::int64_t now = host()->now_unix_ms();
    // 常驻浮层永远排在最前面也不会到期，所以不能只看队首：
    // 扫一遍，把中间所有已到期的限时浮层都收掉。
    for (std::size_t i = toasts_.size(); i > 0; --i) {
      if (toasts_[i - 1].expire_unix_ms <= now) {
        toasts_.erase(toasts_.begin() + static_cast<std::ptrdiff_t>(i - 1));
      }
    }
    TrimToLimit();
    if (toasts_.empty()) return;

    // 还有常驻浮层：续租通知表面。最后一张被关掉后这里不再续租，
    // 宿主会在 kPersistKeepAliveMs 到期后销毁这张表面。
    if (HasPersistentToast()) host()->request_ui_surface(kPersistKeepAliveMs);

    const bool show_actions = host()->setting_bool("show_actions", true);
    const bool show_details = host()->setting_bool("show_details_button", true);
    const bool show_read = host()->setting_bool("show_read_button", true);
    const WidthRange widths = ToastWidths();
    float wrap_width = widths.max - ImGui::GetStyle().WindowPadding.x * 2.0f;
    if (wrap_width < 80.0f) wrap_width = 80.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(widths.min, 0.0f),
                                        ImVec2(widths.max, FLT_MAX));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("###sm_toasts.notifier.imgui", nullptr, flags)) {
      ImGui::End();
      return;
    }

    std::vector<std::int64_t> acked;

    for (std::size_t i = 0; i < toasts_.size(); ++i) {
      const Toast& toast = toasts_[i];
      if (i > 0) ImGui::Separator();

      ImGui::PushID(static_cast<int>(toast.notice.seq));
      ImGui::BeginGroup();

      const IconView icon = FetchIcon(toast.notice.icon);
      if (icon.reserve) {
        const float icon_size = ToastIconSize();
        ImGui::BeginGroup();
        DrawIcon(icon.texture, icon.width, icon.height, icon_size);
        ImGui::EndGroup();
        ImGui::SameLine();
      }

      ImGui::BeginGroup();
      float toast_wrap = wrap_width;
      if (icon.reserve) {
        toast_wrap -= ToastIconSize() + ImGui::GetStyle().ItemSpacing.x;
        if (toast_wrap < 80.0f) toast_wrap = 80.0f;
      }

      ImGui::TextColored(LevelColor(toast.notice.level), "[%s] %s",
                         LevelText(toast.notice.level), toast.notice.title.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("%s", AgoText(now, toast.notice.time_unix_ms).c_str());

      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + toast_wrap);
      ImGui::TextUnformatted(toast.notice.summary.c_str());
      ImGui::PopTextWrapPos();

      bool button_row = false;
      if (!toast.notice.url.empty()) {
        if (ImGui::SmallButton("打开链接")) host()->open_url(toast.notice.url.c_str());
        ImGui::SameLine();
        button_row = true;
      }
      if (show_actions && !toast.notice.buttons.empty()) {
        for (const Button& button : toast.notice.buttons) {
          if (ImGui::SmallButton(button.label.c_str())) {
            host()->invoke_action(toast.notice.seq, button.id.c_str());
          }
          ImGui::SameLine();
        }
        button_row = true;
      }

      if (show_details && !HasDeclaredAction(toast, "details")) {
        if (ImGui::SmallButton("详情")) host()->invoke_action(toast.notice.seq, "details");
        ImGui::SameLine();
        button_row = true;
      }
      if (show_read && !HasDeclaredAction(toast, "dismiss")) {
        if (ImGui::SmallButton("已读")) {
          host()->invoke_action(toast.notice.seq, "dismiss");
          acked.push_back(toast.notice.seq);
        }
        ImGui::SameLine();
        button_row = true;
      }
      if (button_row) ImGui::NewLine();

      ImGui::EndGroup();  // 文本 + 按钮
      ImGui::EndGroup();  // 图标 + 文本，整行都可右键
      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        host()->invoke_action(toast.notice.seq, "dismiss");
        acked.push_back(toast.notice.seq);
      }
      ImGui::PopID();
    }

    for (const std::int64_t seq : acked) RemoveToast(seq);

    ImGui::End();
  }

  void draw_view() override {
    if (!detail_.open) return;
    if (host()->imgui_context() == nullptr) return;

    const NoticeCopy& notice = detail_.notice;
    ImGui::TextColored(LevelColor(notice.level), "[%s]", LevelText(notice.level));
    ImGui::SameLine();
    ImGui::TextDisabled("%s · %s", notice.topic.c_str(),
                        AgoText(host()->now_unix_ms(), notice.time_unix_ms).c_str());
    ImGui::Separator();

    const IconView icon = FetchIcon(notice.icon);
    if (icon.reserve) {
      ImGui::BeginGroup();
      DrawIcon(icon.texture, icon.width, icon.height, DetailIconSize());
      ImGui::EndGroup();
      ImGui::SameLine();
      ImGui::BeginGroup();
    }

    ImGui::TextUnformatted(notice.title.c_str());
    if (!notice.source.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("（来自 %s）", notice.source.c_str());
    }

    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::InputTextMultiline("##sm_detail_body",
                              const_cast<char *>(notice.content.c_str()),
                              notice.content.length(),
                              ImVec2(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x, -footer),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

    if (icon.reserve) ImGui::EndGroup();

    const bool show_actions = host()->setting_bool("show_actions", true);
    if (!notice.url.empty()) {
      if (ImGui::Button("打开链接")) host()->open_url(notice.url.c_str());
      ImGui::SameLine();
    }
    if (show_actions) {
      for (const Button& button : notice.buttons) {
        if (ImGui::Button(button.label.c_str())) {
          host()->invoke_action(notice.seq, button.id.c_str());
        }
        ImGui::SameLine();
      }
    }
    if (ImGui::Button("关闭")) host()->close_view();
  }

 private:
  struct Button {
    std::string id;
    std::string label;
  };

  struct NoticeCopy {
    std::int64_t seq = 0;
    sm::Level level = sm::Level::Info;
    std::int64_t time_unix_ms = 0;
    std::string topic;
    std::string title;
    std::string summary;
    std::string content;
    std::string source;
    std::string url;
    std::string icon;
    std::vector<Button> buttons;
  };

  struct IconView {
    bool          reserve = false;  // 来源非空且没失败：占住图标的位置
    std::uint64_t texture = 0;
    int           width = 0;
    int           height = 0;
  };

  static float ToastIconSize() {
    const float size = ImGui::GetFontSize() * 2.4f;
    return size < 32.0f ? 32.0f : (size > 56.0f ? 56.0f : size);
  }

  static float DetailIconSize() {
    const float size = ImGui::GetFontSize() * 4.0f;
    return size < 48.0f ? 48.0f : (size > 96.0f ? 96.0f : size);
  }

  IconView FetchIcon(const std::string& source) {
    IconView icon;
    if (source.empty()) return icon;

    bool failed = false;
    icon.texture =
        host()->icon_texture(source.c_str(), &icon.width, &icon.height, &failed);
    icon.reserve = !failed;
    return icon;
  }

  static void DrawIcon(std::uint64_t texture, int width, int height, float size) {
    if (texture != 0 && width > 0 && height > 0) {
      // 非正方形图片按中心裁切，图标框保持正方形，布局不受图片比例影响。
      float u0 = 0.0f;
      float v0 = 0.0f;
      float u1 = 1.0f;
      float v1 = 1.0f;
      if (width > height) {
        const float keep = static_cast<float>(height) / static_cast<float>(width);
        u0 = (1.0f - keep) * 0.5f;
        u1 = 1.0f - u0;
      } else if (height > width) {
        const float keep = static_cast<float>(width) / static_cast<float>(height);
        v0 = (1.0f - keep) * 0.5f;
        v1 = 1.0f - v0;
      }
      ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(size, size),
                   ImVec2(u0, v0), ImVec2(u1, v1));
      return;
    }

    // 还在下载/解码：先占住同样大小的位置，图标就绪时整行不会跳动。
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size, pos.y + size);
    draw->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.55f), 4.0f);
    draw->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_Border, 0.8f), 4.0f);
  }

  struct Toast {
    NoticeCopy notice;
    std::int64_t expire_unix_ms = 0;
  };

  struct Detail {
    bool open = false;
    NoticeCopy notice;
  };

  static std::string CopyOrEmpty(const char* text) {
    return (text != nullptr) ? std::string(text) : std::string();
  }

  NoticeCopy CopyNotice(const sm::Notification& n) {
    NoticeCopy copy;
    copy.seq = n.seq;
    copy.level = n.level;
    copy.time_unix_ms = n.time_unix_ms;
    copy.topic = CopyOrEmpty(n.topic);
    copy.title = CopyOrEmpty(n.title);
    copy.summary = CopyOrEmpty(n.summary);
    copy.content = CopyOrEmpty(n.content);
    copy.source = CopyOrEmpty(n.source_plugin);
    copy.url = CopyOrEmpty(n.url);
    copy.icon = CopyOrEmpty(n.icon);
    copy.buttons.reserve(n.actions.length());
    for (const sm::Action& action : n.actions) {
      Button button;
      button.id = CopyOrEmpty(action.id);
      button.label = CopyOrEmpty(action.label);
      if (button.label.empty()) button.label = button.id;
      copy.buttons.push_back(std::move(button));
    }
    return copy;
  }

  bool HasPersistentToast() const {
    for (const Toast& toast : toasts_) {
      if (toast.expire_unix_ms == kNeverExpiresMs) return true;
    }
    return false;
  }

  static bool HasDeclaredAction(const Toast& toast, const char* id) {
    for (const Button& button : toast.notice.buttons) {
      if (button.id == id) return true;
    }
    return false;
  }

  struct WidthRange {
    float min = 320.0f;
    float max = 460.0f;
  };

  WidthRange ToastWidths() {
    std::int64_t min_width = host()->setting_int("toast_width_min", 320);
    std::int64_t max_width = host()->setting_int("toast_width_max", 460);
    if (min_width < 200) min_width = 200;
    if (max_width < 240) max_width = 240;
    if (min_width > max_width) min_width = max_width;  // schema 表达不了跨项约束，只能在读取处夹取
    WidthRange range;
    range.min = static_cast<float>(min_width);
    range.max = static_cast<float>(max_width);
    return range;
  }

  std::int64_t DurationMs() {
    std::int64_t secs = host()->setting_int("toast_duration_sec", 5);
    if (secs < 0) secs = 0;  // 0 表示常驻：不自动消失
    return secs * 1000;
  }

  std::size_t MaxToasts() {
    std::int64_t max_toasts = host()->setting_int("max_toasts", 3);
    if (max_toasts < 1) max_toasts = 1;
    if (max_toasts > 10) max_toasts = 10;
    return static_cast<std::size_t>(max_toasts);
  }

  void TrimToLimit() {
    const std::size_t limit = MaxToasts();
    while (toasts_.size() > limit) toasts_.erase(toasts_.begin());
  }

  void RemoveToast(std::int64_t seq) {
    for (std::size_t i = 0; i < toasts_.size(); ++i) {
      if (toasts_[i].notice.seq == seq) {
        toasts_.erase(toasts_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }

  std::vector<Toast> toasts_;
  Detail            detail_;
  std::int64_t      last_seq_ = 0;
};

}  // namespace

SM_DEFINE_PLUGIN(kMeta, ImGuiNotifier);

// SimpleMessage — 平台层：提示音试听
#pragma once

#include <string>

namespace sm {
namespace platform {

// 播放一段提示音用于设置界面的"试听"按钮。
// spec 是 WAV / MP3 文件路径（相对路径按 base_dir 解析）；不支持其它格式。
// 返回 false 时 error 里是可直接展示给用户的 UTF-8 原因。
bool PreviewSound(const std::string& spec, const std::string& base_dir,
                  std::string* error);

}  // namespace platform
}  // namespace sm

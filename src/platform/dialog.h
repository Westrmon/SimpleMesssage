// SimpleMessage — 平台层：系统文件对话框
#pragma once

#include <string>

namespace sm {
namespace platform {

struct SaveFileDialogOptions {
  std::string default_name;
  std::string initial_directory;
  std::string filter_label;      // 例如 "JSON Lines (*.jsonl)"
  std::string filter_extension;  // 例如 "jsonl"
  std::string all_files_label;   // 例如 "所有文件 (*.*)"
};

// 弹出系统"另存为"对话框；用户取消时返回空字符串。
std::string ShowSaveFileDialog(const SaveFileDialogOptions& options);

struct OpenFileDialogOptions {
  std::string initial_directory;
  std::string filter_label;      // 例如 "WAV 文件 (*.wav)"
  std::string filter_extension;  // 例如 "wav"
  std::string all_files_label;   // 例如 "所有文件 (*.*)"
};

// 弹出系统"打开"对话框；用户取消时返回空字符串。
std::string ShowOpenFileDialog(const OpenFileDialogOptions& options);

}  // namespace platform
}  // namespace sm

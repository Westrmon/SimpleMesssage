// SimpleMessage — Win32 平台层：系统文件对话框

#include "platform/dialog.h"

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <commdlg.h>

#include "platform/utf8.h"

namespace sm {
namespace platform {
namespace {

// 32768 是 Win32 对 OPENFILENAMEW.lpstrFile 允许的上限（MAX_PATH 只有 260）。
constexpr std::size_t kPathBufferChars = 32768;

std::wstring BuildFilter(const SaveFileDialogOptions& options) {
  std::wstring filter;
  filter += utf8::widen(options.filter_label.c_str());
  filter.push_back(L'\0');
  filter += L"*." + utf8::widen(options.filter_extension.c_str());
  filter.push_back(L'\0');
  filter += utf8::widen(options.all_files_label.c_str());
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');
  return filter;
}

}  // namespace

std::string ShowSaveFileDialog(const SaveFileDialogOptions& options) {
  std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
  const std::wstring initial_name = utf8::widen(options.default_name.c_str());
  const std::size_t name_chars =
      (std::min)(initial_name.size(), buffer.size() - 1);
  std::copy(initial_name.begin(), initial_name.begin() + name_chars, buffer.begin());

  const std::wstring initial_dir = utf8::widen(options.initial_directory.c_str());
  const std::wstring filter = BuildFilter(options);
  const std::wstring default_ext = utf8::widen(options.filter_extension.c_str());

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = ::GetActiveWindow();
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = buffer.data();
  ofn.nMaxFile = static_cast<DWORD>(buffer.size());
  ofn.lpstrDefExt = default_ext.empty() ? nullptr : default_ext.c_str();
  ofn.lpstrInitialDir = initial_dir.empty() ? nullptr : initial_dir.c_str();
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (::GetSaveFileNameW(&ofn) == 0) return std::string();
  return utf8::narrow(buffer.data());
}

}  // namespace platform
}  // namespace sm

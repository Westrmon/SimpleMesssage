// SimpleMessage — 文件系统原语（UTF-8 路径）
//
// 所有路径都按 UTF-8 解释。Windows 实现转发到宽字符 API，避免 C++17
// std::filesystem 在 MSVC 上把 char* 当作 ANSI 代码页的问题。
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sm {
namespace platform {
namespace fs {

// 以 UTF-8 路径打开文件；mode 支持 "rb" / "wb" / "ab"。
// "ab" 在 Windows 上以允许其他进程读取的方式共享打开（日志文件需要）。
std::FILE* open_file(const char* utf8_path, const char* mode);

bool ensure_directory(const std::string& utf8_dir);
bool file_exists(const std::string& utf8_path);
bool remove_file(const std::string& utf8_path);
bool rename_file(const std::string& from, const std::string& to);

struct DirEntry {
  std::string   name;
  std::uint64_t size;
};

// 只返回普通文件；按 name 排序。name_suffix 为空表示不过滤。
std::vector<DirEntry> list_files(const std::string& utf8_dir,
                                 const char* name_suffix);

bool is_absolute_path(const std::string& utf8_path);

}  // namespace fs
}  // namespace platform
}  // namespace sm

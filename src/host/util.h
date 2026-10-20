// SimpleMessage — 宿主内部通用工具
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/fs.h"

namespace sm {
namespace util {

std::string path_join(const std::string& dir, const std::string& name);
std::string path_dir(const std::string& path);
std::string path_filename(const std::string& path);
bool        path_is_absolute(const std::string& path);

bool ensure_directory(const std::string& utf8_dir);
bool is_directory_writable(const std::string& utf8_dir);
bool file_exists(const std::string& utf8_path);
bool remove_file(const std::string& utf8_path);
bool rename_file(const std::string& from, const std::string& to);

bool read_text_file(const std::string& utf8_path, std::string* out_text,
                    std::string* out_error = nullptr);

bool write_text_file_atomically(const std::string& utf8_path, const std::string& text,
                                std::string* out_error = nullptr);

// 截断到 max_bytes，且不切断 UTF-8 多字节序列。返回是否真的截断了。
bool TruncateUtf8(std::string* text, std::size_t max_bytes);

using DirEntry = platform::fs::DirEntry;

std::vector<DirEntry> list_files(const std::string& utf8_dir, const char* name_suffix);

std::int64_t now_unix_ms();

void format_short_time(std::int64_t time_unix_ms, std::int64_t now_unix_ms, char* out,
                       std::size_t capacity);

void format_duration(std::int64_t ms, char* out, std::size_t capacity);

const char* thread_tag();
void        set_thread_tag(const char* tag);

}  // namespace util
}  // namespace sm

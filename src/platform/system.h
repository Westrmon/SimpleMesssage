// SimpleMessage — 平台层：进程与系统环境
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace sm {
namespace platform {

// 可执行文件所在目录（UTF-8）。
std::string exe_directory();

// 当前进程的完整命令行（UTF-8，索引 0 是程序路径）。
std::vector<std::string> command_line_args();

// 把 main(argc, argv) 转成 UTF-8 参数表；与平台无关。
inline std::vector<std::string> args_from_argv(int argc, char** argv) {
  std::vector<std::string> args;
  if (argv == nullptr) return args;
  for (int i = 0; i < argc && argv[i] != nullptr; ++i) {
    args.emplace_back(argv[i]);
  }
  return args;
}

// 用系统默认程序打开一个路径/URL。
bool shell_open(const char* utf8_target);

// 系统字体目录与首选字体文件名（按优先级）。
std::string system_font_directory();
std::vector<std::string> preferred_font_files();

// 把时间转成本地时间；成功返回 true。
bool local_time(std::time_t time, std::tm* out);

// 用户界面上显示的 CPU 架构名。
const char* architecture_name();

// 本平台的首选路径分隔符（'\\' 或 '/'）。
char path_separator();

// 用户级配置根目录（例如 %APPDATA%\SimpleMessage）。
std::string user_config_root();

}  // namespace platform
}  // namespace sm

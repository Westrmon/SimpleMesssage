// SimpleMessage — imgui 共享库边界上的 IMGUI_API 定义
//
// 【这个文件为什么存在】
// imgui 在本项目里被编译成 imgui.dll（为什么必须共享而不是各自静态编译，
// 见 external/CMakeLists.txt 与 docs/插件开发/01-入门.md §7「imgui 共享约定」）。要在 DLL 边界
// 上正确工作，编译 imgui.dll 自身时 IMGUI_API 必须是 __declspec(dllexport)，
// 而使用它的宿主与插件必须是 __declspec(dllimport)。
//
// 直觉做法是在 CMake 里写
//     target_compile_definitions(sm_imgui PRIVATE IMGUI_API=__declspec(dllexport))
// 但这是一个【会静默出错的陷阱】：CMake 认为"函数式"的宏定义不能可靠地
// 传给编译器，会把括号里的部分丢掉，只留下
//     -DIMGUI_API=__declspec -Ddllexport -D)
// 于是 imgui.dll 根本没有导出任何符号，插件链接时才以一种难以定位的方式失败
// （表现为 ERROR_MOD_NOT_FOUND(126)，见 docs/插件开发/01-入门.md §7.2）。
//
// CMake 自己的提示就是正解："Consider defining the macro in a (configured)
// header file." imgui 恰好提供了官方钩子 IMGUI_USER_CONFIG：imgui.h 在
// 定义 IMGUI_API 之前会先 #include 这个头（imgui.h 第 65 行），所以在这里
// 定义 IMGUI_API 是官方支持的用法，且不依赖任何编译器的命令行语法。
//
// 【为什么用自定义宏而不是 CMake 自动生成的 sm_imgui_EXPORTS】
// 后者由目标名推导而来 —— 目标一旦改名，本文件会安静地退化成 dllimport，
// 而"编译 DLL 时用 dllimport"是个能把人绕半天的错误。SM_IMGUI_BUILDING_DLL
// 由 external/CMakeLists.txt 显式给出，改名时会立刻编译失败而不是静默走偏。

#pragma once

#if defined(_WIN32)
#ifdef SM_IMGUI_BUILDING_DLL
#define IMGUI_API __declspec(dllexport)
#else
#define IMGUI_API __declspec(dllimport)
#endif
#else
// 非 Windows 平台不需要显式的导出标记（默认全导出）。
// POSIX 平台层当前只有空壳，见 docs/01-架构总览.md §6。
#define IMGUI_API
#endif

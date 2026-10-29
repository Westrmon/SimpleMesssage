// SimpleMessage — imgui 共享库边界上的 IMGUI_API 定义

#pragma once

#if defined(_WIN32)
#ifdef SM_IMGUI_BUILDING_DLL
#define IMGUI_API __declspec(dllexport)
#else
#define IMGUI_API __declspec(dllimport)
#endif
#else
#define IMGUI_API
#endif

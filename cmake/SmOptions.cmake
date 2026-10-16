# SimpleMessage — 构建选项与版本一致性检查

include_guard(GLOBAL)

option(SM_BUILD_HOST
       "构建宿主可执行文件（本仓库当前只包含 CMake 骨架与占位 main）" ON)
option(SM_BUILD_SAMPLES
       "构建随附的示例插件（source.email / source.webhook / notifier.imgui / notifier.console）" ON)
option(SM_BUILD_TEST_PLUGINS
       "构建测试坏插件（plugins/_test 与 plugins/_badmeta）" OFF)
option(SM_BUILD_TESTS "构建 SDK 纯函数单元测试（ctest 驱动）" ON)

# Win7 兼容：静态 CRT、禁用现代清单项、把 _WIN32_WINNT 拉回 0x0601。
# 这不能弥补"现代工具集已不再保证 Win7 目标"的事实 —— 详见
# docs/01-架构总览.md §6.2。
option(SM_WIN7_COMPAT "为 Windows 7 SP1 做最佳努力的兼容配置" OFF)
option(SM_EMBED_CJK_FONT "把中文字体内嵌进宿主而不是从系统加载" OFF)
set(SM_EMBED_CJK_FONT_FILE "" CACHE FILEPATH
    "SM_EMBED_CJK_FONT 打开时要内嵌的字体文件（.ttf/.ttc）")
option(SM_WARNINGS_AS_ERRORS "把编译警告当作错误" ON)

function(sm_assert_version_header)
    set(_header "${CMAKE_CURRENT_SOURCE_DIR}/include/sm/version.h")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR "找不到 ${_header}")
    endif()

    file(STRINGS "${_header}" _major_line REGEX "^#define SM_VERSION_MAJOR ")
    file(STRINGS "${_header}" _minor_line REGEX "^#define SM_VERSION_MINOR ")
    file(STRINGS "${_header}" _patch_line REGEX "^#define SM_VERSION_PATCH ")

    string(REGEX MATCH "[0-9]+" _h_major "${_major_line}")
    string(REGEX MATCH "[0-9]+" _h_minor "${_minor_line}")
    string(REGEX MATCH "[0-9]+" _h_patch "${_patch_line}")

    set(_header_version "${_h_major}.${_h_minor}.${_h_patch}")
    if(NOT _header_version STREQUAL "${PROJECT_VERSION}")
        message(FATAL_ERROR
            "版本号不一致：CMakeLists.txt 是 ${PROJECT_VERSION}，"
            "include/sm/version.h 是 ${_header_version}。"
            "请同时修改两处（发布时二者必须恒等）。")
    endif()
endfunction()

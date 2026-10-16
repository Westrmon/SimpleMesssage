# SimpleMessage — 平台层设置
include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# 平台识别
# ---------------------------------------------------------------------------
if(WIN32)
    set(SM_PLATFORM "win32" CACHE INTERNAL "当前平台层实现目录名")
elseif(UNIX)
    set(SM_PLATFORM "posix" CACHE INTERNAL "当前平台层实现目录名")
else()
    message(FATAL_ERROR
        "暂不支持的平台：${CMAKE_SYSTEM_NAME}。"
        "当前只提供 win32 平台层（posix 只有空壳）。")
endif()

# ---------------------------------------------------------------------------
# sm_apply_platform_settings(<target>)
# ---------------------------------------------------------------------------
function(sm_apply_platform_settings target)
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform")

    if(SM_WIN7_COMPAT)
        if(SM_PLATFORM STREQUAL "win32")
            # _WIN32_WINNT/WINVER 拉到 Windows 7 SP1：这只会改变 SDK 头的
            # 可见 API 面，让"不小心用了 Win8+ 独有 API"在编译期就暴露出来。
            target_compile_definitions(${target} PRIVATE
                _WIN32_WINNT=0x0601
                WINVER=0x0601
                NTDDI_VERSION=0x06010000)

            # 静态 CRT：不要求目标机器安装 VC 运行时分发。
            # 注意 UCRT 仍然需要目标机器打过 KB2999226 —— 这一条无法靠编译选项
            # 解决，只能写进安装说明。
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

            # 现代清单项在旧系统上可能被拒或行为异常，Win7 兼容模式下关掉。
            target_link_options(${target} PRIVATE /MANIFEST:NO)
            target_compile_definitions(${target} PRIVATE SM_WIN7_COMPAT=1)

            message(STATUS
                "  [${target}] Win7 兼容模式：_WIN32_WINNT=0x0601，静态 CRT，无清单。"
                " 这不能弥补现代工具集已不再保证 Win7 目标的事实，"
                "见 docs/01-架构总览.md §6.2。")
        else()
            message(WARNING "SM_WIN7_COMPAT=ON 但当前平台是 ${SM_PLATFORM}，已忽略。")
        endif()
    else()
        if(SM_PLATFORM STREQUAL "win32")
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /permissive- /utf-8 /W4 /EHsc)

        if(SM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -fvisibility=hidden)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# sm_apply_plugin_settings(<target>)
# ---------------------------------------------------------------------------
function(sm_apply_plugin_settings target)
    if(SM_WIN7_COMPAT)
        if(SM_PLATFORM STREQUAL "win32")
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
    else()
        if(SM_PLATFORM STREQUAL "win32")
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /permissive- /utf-8 /W4 /EHsc)
        if(SM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()

    # 插件只导出 sm_* 那四个符号，其余一概隐藏。
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endfunction()

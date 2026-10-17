# SimpleMessage — 平台层设置
include_guard(GLOBAL)

if(WIN32)
    set(SM_PLATFORM "win32" CACHE INTERNAL "当前平台层实现目录名")
elseif(UNIX)
    set(SM_PLATFORM "posix" CACHE INTERNAL "当前平台层实现目录名")
else()
    message(FATAL_ERROR
        "暂不支持的平台：${CMAKE_SYSTEM_NAME}。"
        "当前只提供 win32 平台层（posix 只有空壳）。")
endif()

function(sm_apply_platform_settings target)
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform")

    if(SM_WIN7_COMPAT)
        if(SM_PLATFORM STREQUAL "win32")
            target_compile_definitions(${target} PRIVATE
                _WIN32_WINNT=0x0601
                WINVER=0x0601
                NTDDI_VERSION=0x06010000)

            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
            target_link_options(${target} PRIVATE /MANIFEST:NO)
            target_compile_definitions(${target} PRIVATE SM_WIN7_COMPAT=1)

            message(STATUS
                "  [${target}] Win7 兼容模式：_WIN32_WINNT=0x0601，静态 CRT，无清单。")
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

    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endfunction()

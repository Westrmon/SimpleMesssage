# SimpleMessage — 从 git tag 推导版本号
#
# 版本只有一个来源：仓库的 git tag（形如 v0.1.0）。CMake 配置时用
# `git describe` 找到离 HEAD 最近的匹配 tag，取其数字部分作为
# PROJECT_VERSION / SM_VERSION_*；完整 describe 串（含提交距离、哈希、
# dirty 标记）作为 SM_VERSION_DETAIL，供界面与日志排障。
#
# 没有 git、仓库没有匹配 tag 时退回 include/sm/version.h 里的兜底值，
# 并在配置阶段给出警告。这样从源码压缩包构建不会直接失败。

include_guard(GLOBAL)

# 读取 version.h 中 #ifndef 兜底宏的值。
function(_sm_version_from_header repo_root out_var)
    set(_header "${repo_root}/include/sm/version.h")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR "找不到 ${_header}")
    endif()

    file(STRINGS "${_header}" _major_line REGEX "^#define SM_VERSION_MAJOR ")
    file(STRINGS "${_header}" _minor_line REGEX "^#define SM_VERSION_MINOR ")
    file(STRINGS "${_header}" _patch_line REGEX "^#define SM_VERSION_PATCH ")

    string(REGEX MATCH "[0-9]+" _major "${_major_line}")
    string(REGEX MATCH "[0-9]+" _minor "${_minor_line}")
    string(REGEX MATCH "[0-9]+" _patch "${_patch_line}")
    if(_major STREQUAL "" OR _minor STREQUAL "" OR _patch STREQUAL "")
        message(FATAL_ERROR "无法从 ${_header} 读取 SM_VERSION_MAJOR/MINOR/PATCH")
    endif()

    set(${out_var} "${_major}.${_minor}.${_patch}" PARENT_SCOPE)
endfunction()

# sm_detect_version(<仓库根目录>)
# 输出（PARENT_SCOPE）：
#   SM_VERSION         —— 严格数字的 major.minor.patch，供 project() 与 host_range 判定
#   SM_VERSION_DETAIL  —— git describe 的完整结果，仅用于展示/排障
function(sm_detect_version repo_root)
    set(_version "")
    set(_detail "")
    set(_source "")

    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${repo_root}"
                    describe --tags --always --dirty --match "v[0-9]*"
            OUTPUT_VARIABLE _describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _result
            ERROR_QUIET)
        # tag 形如 v0.1.0 / v0.1.0-3-gabc1234 / v0.1.0-dirty；
        # 后面两种仍取数字部分，后缀只进 DETAIL。
        if(_result EQUAL 0 AND _describe MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
            set(_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
            set(_detail "${_describe}")
            set(_source "git tag")
        endif()
    endif()

    if(_version STREQUAL "")
        _sm_version_from_header("${repo_root}" _fallback)
        set(_version "${_fallback}")
        set(_detail "${_fallback}-nogit")
        set(_source "version.h 兜底值")
        message(WARNING
            "未能从 git tag 确定版本（没有 git，或仓库里没有 vX.Y.Z 形式的 tag）；"
            "退回 ${_fallback}。请检查构建环境是否带 .git 与 tags。")
    else()
        message(STATUS "版本：${_version}（来自 ${_source}：${_detail}）")
    endif()

    set(SM_VERSION "${_version}" PARENT_SCOPE)
    set(SM_VERSION_DETAIL "${_detail}" PARENT_SCOPE)
endfunction()

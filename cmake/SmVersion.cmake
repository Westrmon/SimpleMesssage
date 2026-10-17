# SimpleMessage — 从 git tag 推导版本号

include_guard(GLOBAL)

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

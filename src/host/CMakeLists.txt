# SimpleMessage — 宿主可执行文件

if(NOT SM_HAVE_IMGUI OR NOT SM_HAVE_TOMLPP OR NOT SM_HAVE_MINIAUDIO)
    message(STATUS
        "跳过 sm_host：第三方依赖不完整"
        "（imgui=${SM_HAVE_IMGUI}, toml++=${SM_HAVE_TOMLPP}, "
        "miniaudio=${SM_HAVE_MINIAUDIO}）。"
        "三者都是随仓库提交的普通文件，缺失说明有人删过；"
        "重新获取见 external/PROVENANCE.txt 与 docs/02-宿主设计.md §20。")
    return()
endif()

add_executable(sm_host
    main.cpp
    app.cpp
    util.cpp
    log.cpp
    config.cpp
    instances.cpp
    monitor.cpp
    seh_guard.cpp
    plugin_host.cpp
    plugin_manager.cpp
    scheduler.cpp
    router.cpp
    rules.cpp
    rule_time.cpp
    placeholders.cpp
    rule_save.cpp
    notification_pipeline.cpp
    action_dispatch.cpp
    internal_source.cpp
    ui_fonts.cpp
    image_cache.cpp
    render_probe.cpp
    render_opengl.cpp
    ui_root.cpp
    ui_session.cpp
    ui_state.cpp
    text_input.cpp
    settings_form.cpp
    notify_view.cpp
    ui_panel.cpp
    panel_pages.cpp
    sm_host.rc
)

target_include_directories(sm_host PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(sm_host PRIVATE
    sm::sdk
    sm::imgui
    sm::tomlplusplus
    sm::miniaudio
)

target_sources(sm_host PRIVATE ${SM_IMGUI_BACKEND_SOURCES})

if(SM_HAVE_GLFW)
    target_link_libraries(sm_host PRIVATE sm::glfw)
endif()

if(SM_HAVE_CURL)
    target_link_libraries(sm_host PRIVATE sm::curl)
    target_compile_definitions(sm_host PRIVATE SM_HAVE_CURL=1)
endif()

if(WIN32)
    target_compile_definitions(sm_host PRIVATE UNICODE _UNICODE)
endif()
target_compile_definitions(sm_host PRIVATE IMGUI_IMPL_API=)
set(SM_PLATFORM_DIR "${CMAKE_SOURCE_DIR}/src/platform/${SM_PLATFORM}")
target_sources(sm_host PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/utf8.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/sound.cpp"
    "${SM_PLATFORM_DIR}/platform_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/crash_guard_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/single_instance_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/tray_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/window_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/icon_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/fs_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/system_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/console_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/message_window_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/dialog_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/image_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/monitor_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/entry_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/render_dx11_${SM_PLATFORM}.cpp")
if(MSVC)
    set_source_files_properties(
        "${SM_PLATFORM_DIR}/crash_guard_${SM_PLATFORM}.cpp"
        PROPERTIES COMPILE_OPTIONS "/EHa")
endif()

sm_apply_platform_settings(sm_host)

if(WIN32)
    set_target_properties(sm_host PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(sm_host PRIVATE
        d3d11 d3dcompiler dxgi opengl32 shell32 user32 dwmapi comdlg32
        windowscodecs ole32)
endif()

target_compile_definitions(sm_host PRIVATE
    SM_DEFAULT_PLUGIN_DIR="plugins"
    SM_DEFAULT_CONFIG_FILE="config.toml"
)

install(TARGETS sm_host RUNTIME DESTINATION .)

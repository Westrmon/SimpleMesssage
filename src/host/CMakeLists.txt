# SimpleMessage — 宿主可执行文件
#
# 模块划分与职责见 docs/02-宿主设计.md 的"模块划分"一节。

if(NOT SM_HAVE_IMGUI OR NOT SM_HAVE_TOMLPP)
    message(STATUS
        "跳过 sm_host：第三方依赖不完整"
        "（imgui=${SM_HAVE_IMGUI}, toml++=${SM_HAVE_TOMLPP}）。"
        "两者是随仓库提交的普通文件，缺失说明有人删过；"
        "重新获取见 external/PROVENANCE.txt 与 docs/02-宿主设计.md §20。")
    return()
endif()

add_executable(sm_host
    main.cpp
    app.cpp
    util.cpp
    log.cpp
    config.cpp
    monitor.cpp
    seh_guard.cpp
    plugin_host.cpp
    plugin_manager.cpp
    scheduler.cpp
    router.cpp
    notification_pipeline.cpp
    action_dispatch.cpp
    internal_source.cpp
    ui_fonts.cpp
    message_window.cpp
    render_probe.cpp
    render_dx11.cpp
    render_opengl.cpp
    ui_root.cpp
    ui_session.cpp
    ui_state.cpp
    text_input.cpp
    settings_form.cpp
    notify_view.cpp
    ui_panel.cpp
    panel_pages.cpp
    # 可执行文件里的资源：应用图标（窗口、任务栏与托盘都从这一条取，
    # 见 platform/win32/icon_win32.cpp 与 app_icon_id.h）。
    sm_host.rc
)

# 宿主内部头文件用 "host/xxx.h" 引用，因此源码根目录要挂上。
target_include_directories(sm_host PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(sm_host PRIVATE
    sm::sdk
    sm::imgui
    sm::tomlplusplus
)

target_sources(sm_host PRIVATE ${SM_IMGUI_BACKEND_SOURCES})

# imgui 后端里的 GLFW 那一份、以及窗口层的实现（window_win32.cpp）都直接调用
# glfw*，因此 GLFW 必须链接进宿主。external/ 在 GLFW 缺失时不会把那两个后端放进
# SM_IMGUI_BACKEND_SOURCES，所以这里跟着同一个条件走，保证"依赖这个库的那些源
# 文件"与"这个库"同进同出 —— 否则缺 GLFW 时得到的报错是"链接不到 glfwInit"，
# 看不出真正的原因是依赖被删了。
if(SM_HAVE_GLFW)
    target_link_libraries(sm_host PRIVATE sm::glfw)
endif()

# 宿主是一个 Windows 应用，因此按 Windows 的惯例定义 UNICODE/_UNICODE。
# 代码里所有 Win32 调用都显式写着 W 后缀，所以这不是"用不用宽字符"的开关，
# 而只影响那些按 UNICODE 展开的宏 —— 例如 IDC_ARROW：它展开成 LPSTR 再传给
# LoadCursorW() 会直接编译失败，而那个错误信息看起来和资源 id 毫无关系。
if(WIN32)
    target_compile_definitions(sm_host PRIVATE UNICODE _UNICODE)
endif()

# imgui 后端（imgui_impl_glfw / imgui_impl_dx11）由宿主自己定义并调用，
# 不需要被插件看到。但 imgui_impl_*.h 默认把 IMGUI_IMPL_API 定义成 IMGUI_API，
# 而宿主从 sm::imgui 继承到的 IMGUI_API 是 dllimport —— 用 dllimport 去
# "定义"一个函数会直接编译失败。所以这里显式清空它。
target_compile_definitions(sm_host PRIVATE IMGUI_IMPL_API=)

# 平台层实现。抽象接口是 src/platform/platform.h（窗口那一块在
# src/platform/window.h），实现按平台分目录。窗口实现建立在 GLFW 之上，因此它
# 能不能编译取决于下面那个 sm::glfw 块。
set(SM_PLATFORM_DIR "${CMAKE_SOURCE_DIR}/src/platform/${SM_PLATFORM}")
target_sources(sm_host PRIVATE
    "${SM_PLATFORM_DIR}/platform_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/crash_guard_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/single_instance_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/tray_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/window_${SM_PLATFORM}.cpp"
    "${SM_PLATFORM_DIR}/icon_${SM_PLATFORM}.cpp")

# 崩溃屏障需要 /EHa：只有它才能把"以 SEH 形式抛出的 C++ 异常"也一并捕获。
# 源文件级选项排在目标级选项之后，因此这里的 /EHa 覆盖 /EHsc。
if(MSVC)
    set_source_files_properties(
        "${SM_PLATFORM_DIR}/crash_guard_${SM_PLATFORM}.cpp"
        PROPERTIES COMPILE_OPTIONS "/EHa")
endif()

sm_apply_platform_settings(sm_host)

# 常驻托盘应用不应弹出控制台窗口。
if(WIN32)
    set_target_properties(sm_host PROPERTIES WIN32_EXECUTABLE TRUE)

    # 【为什么必须有 d3dcompiler】imgui 的 DX11 后端在运行时用 D3DCompile()
    # 现场编译自己的着色器。少了它，链接期就是一行
    # "undefined reference to `D3DCompile'"，而这行错误看起来像是 imgui 的问题，
    # 实际上只是漏了一个库。注意它对应的是运行时的 d3dcompiler_47.dll：
    # Win7 上要装 KB2670838（平台更新）才有 —— 与 Win7 上要跑 DXGI 1.2 所需的
    # 是同一个更新，见 docs/01-架构总览.md §6.2。
    #
    # comdlg32 供通知页的"导出…"用：那是系统自己的"另存为"对话框，
    # 侧边栏、新建文件夹、记住上次目录这些都是它带来的，不该由宿主重写一遍。
    #
    # shell32 / user32 / dwmapi 供托盘、单实例与窗口使用。
    #
    # 【opengl32 只为了视口与清屏】OpenGL 回退后端（render_opengl.cpp）用它取
    # glViewport / glClear 这几个 GL 1.1 函数；imgui 自己的着色器管线走它自带的
    # 加载器，不需要我们再准备任何东西。少了这一个库的症状是链接期四行
    # "unresolved external symbol _glClear@4"。
    #
    # 【不需要 shcore】进程的 DPI 感知现在由 GLFW 在建窗口时设定，而它把
    # SetProcessDpiAwarenessContext(user32)、SetProcessDpiAwareness(shcore) 等
    # 一律用 LoadLibrary+GetProcAddress 动态解析（external/glfw/src/win32_init.c
    # 的 loadLibraries）—— 于是 Windows 7 上既不会因为缺少这些新 API 而启动
    # 失败，也不需要在这里补一个 shcore 导入库。
    target_link_libraries(sm_host PRIVATE
        d3d11 d3dcompiler dxgi opengl32 shell32 user32 dwmapi comdlg32)
endif()

# 把插件目录约定告诉宿主（宿主也允许用 --plugin-dir 覆盖）。
target_compile_definitions(sm_host PRIVATE
    SM_DEFAULT_PLUGIN_DIR="plugins"
    SM_DEFAULT_CONFIG_FILE="config.toml"
)

install(TARGETS sm_host RUNTIME DESTINATION .)

# SimpleMessage — 插件目录
#
# 两族插件，规则相同、去处不同：<id>/ 是随仓库发布的示例插件，
# _test/ 与 _badmeta/ 是故意写坏的测试插件。示例插件的设计要点见
# docs/插件开发/04-示例插件.md。

# 插件目标的统一规则：
# 1. 输出名必须是 <插件 id>.dll，与 PluginMeta::id 完全一致（宿主会交叉校验）。
# 2. 只链接 sm::sdk（需要 imgui 时加 sm::imgui），绝不链接平台层。
# 3. 输出目录由调用方给出 —— 正式插件与测试插件必须分开落盘。
function(sm_configure_plugin target output_dir)
    add_library(${target} SHARED "${target}/plugin.cpp")

    target_link_libraries(${target} PRIVATE sm::sdk)

    # 输出目录里的 $<CONFIG> 不可省，原因见顶层 CMakeLists.txt 的"运行期产物布局"。
    set_target_properties(${target} PROPERTIES
        PREFIX ""                                   # 不要 lib<name> 前缀
        OUTPUT_NAME "${target}"                     # 产出 <id>.dll
        RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${output_dir}")

    sm_apply_plugin_settings(${target})
endfunction()

# 正式插件：落在 <build>/<配置>/plugins/，与宿主的 --plugin-dir 默认值对齐。
function(sm_add_plugin target)
    sm_configure_plugin(${target} "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins")
endfunction()

# 测试坏插件：落在独立的 plugins_test/，整目录拷进 plugins/ 即可复现用例。
function(sm_add_test_plugin target)
    sm_configure_plugin(${target} "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins_test")
endfunction()

if(SM_BUILD_SAMPLES)
    sm_add_plugin(notifier.console)

    # ---- 两个源插件：它们要谈网络协议，各带一个第三方依赖 ----
    # 这是对"只链接 sm::sdk"的有意破例；边界是依赖静态链进插件（不新增要部署的
    # DLL），且不得把依赖的类型带过插件边界。见 docs/插件开发/04-示例插件.md。
    if(SM_HAVE_CURL)
        sm_add_plugin(source.email)
        target_link_libraries(source.email PRIVATE sm::curl)
    else()
        message(STATUS "跳过 source.email：libcurl 不可用。")
    endif()

    if(SM_HAVE_HTTPLIB)
        sm_add_plugin(source.webhook)
        target_link_libraries(source.webhook PRIVATE sm::httplib)
    else()
        message(STATUS "跳过 source.webhook：cpp-httplib 不可用。")
    endif()

    # notifier.imgui 拿到的是 imgui.dll 的导入库：插件与宿主必须共享同一个
    # imgui 实例，而不是各编一份。原因见 external/CMakeLists.txt 与
    # docs/插件开发/01-入门.md §7。
    if(SM_HAVE_IMGUI)
        sm_add_plugin(notifier.imgui)
        target_link_libraries(notifier.imgui PRIVATE sm::imgui)
    else()
        message(STATUS "跳过 notifier.imgui：imgui 不可用。")
    endif()

    # 插件在构建输出里对宿主可见即可；安装规则与宿主保持一致的相对布局。
    install(DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins/" DESTINATION plugins
            OPTIONAL)
endif()

if(SM_BUILD_TEST_PLUGINS)
    add_subdirectory(_test)
    add_subdirectory(_badmeta)
endif()

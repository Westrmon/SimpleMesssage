# SimpleMessage — SDK 单元测试
#
# 只依赖 sm::sdk（纯头文件、零第三方依赖），因此在任何环境下都能构建运行 ——
# 这正是"SDK 的纯函数可以被单独测试"这个设计目标的落地方式。
#
# 覆盖 include/sm/ 里那些 header-only 的纯函数：版本范围解析与 UTF-8 截断的
# 边界情况是"看起来对、偶尔错"的典型，而它们分别决定了"插件装不装得上"和
# "界面上是不是一串方块"。
#
# 跑法：ctest --test-dir build -C Release --output-on-failure

add_executable(sm_sdk_tests
    main.cpp
    test_framework.cpp
    test_version_range.cpp
    test_summary.cpp
)

target_link_libraries(sm_sdk_tests PRIVATE sm::sdk)

# 与宿主共用同一套编译设置（/W4 /WX、/utf-8、CRT 链接方式）。它顺带挂上的
# 平台层头目录测试用不到 —— 为了不把这套设置复制第三份，这点冗余是划算的。
sm_apply_platform_settings(sm_sdk_tests)

add_test(NAME sm_sdk_tests COMMAND sm_sdk_tests)

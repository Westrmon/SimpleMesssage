# SimpleMessage — 构建选项

include_guard(GLOBAL)

option(SM_BUILD_HOST
       "构建宿主可执行文件（本仓库当前只包含 CMake 骨架与占位 main）" ON)
option(SM_BUILD_SAMPLES
       "构建随附的示例插件（source.email / source.gotify / source.webhook / notifier.imgui / notifier.console / notifier.sound / notifier.wintoast）" ON)
option(SM_BUILD_TEST_PLUGINS
       "构建测试坏插件（plugins/_test 与 plugins/_badmeta）" OFF)
option(SM_BUILD_TESTS "构建 SDK 纯函数单元测试（ctest 驱动）" ON)

# Win7 兼容：静态 CRT、禁用现代清单项、把 _WIN32_WINNT 拉回 0x0601。
option(SM_WIN7_COMPAT "为 Windows 7 SP1 做最佳努力的兼容配置" OFF)
option(SM_EMBED_CJK_FONT "把中文字体内嵌进宿主而不是从系统加载" OFF)
set(SM_EMBED_CJK_FONT_FILE "" CACHE FILEPATH
    "SM_EMBED_CJK_FONT 打开时要内嵌的字体文件（.ttf/.ttc）")
option(SM_WARNINGS_AS_ERRORS "把编译警告当作错误" ON)

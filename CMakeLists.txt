cmake_minimum_required(VERSION 3.20)

project(SimpleMessage
        VERSION 0.1.0
        DESCRIPTION "基于插件机制的通知聚合与投递工具"
        LANGUAGES CXX)

# CMP0091：让 MSVC_RUNTIME_LIBRARY 属性生效（而不是靠改写 CMAKE_CXX_FLAGS）。
# Win7 兼容模式要切静态 CRT，必须用到它。
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# ---------------------------------------------------------------------------
# 运行期产物布局
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SmOptions)
include(SmPlatform)

sm_assert_version_header()

# ---------------------------------------------------------------------------
# sm_sdk —— SDK 公共头（纯头文件，零依赖）
# ---------------------------------------------------------------------------
add_library(sm_sdk INTERFACE)
add_library(sm::sdk ALIAS sm_sdk)
target_include_directories(sm_sdk INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(sm_sdk INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# 第三方依赖（imgui / toml++ / GLFW）
# ---------------------------------------------------------------------------
add_subdirectory(external)

# ---------------------------------------------------------------------------
# 宿主可执行文件
# ---------------------------------------------------------------------------
if(SM_BUILD_HOST)
    add_subdirectory(src/host)
endif()

# ---------------------------------------------------------------------------
# 插件
# ---------------------------------------------------------------------------
if(SM_BUILD_SAMPLES OR SM_BUILD_TEST_PLUGINS)
    add_subdirectory(plugins)
endif()

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(SM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# 配置摘要
# ---------------------------------------------------------------------------
message(STATUS "SimpleMessage ${PROJECT_VERSION}")
message(STATUS "  构建宿主      SM_BUILD_HOST       = ${SM_BUILD_HOST}")
message(STATUS "  示例插件      SM_BUILD_SAMPLES    = ${SM_BUILD_SAMPLES}")
message(STATUS "  测试坏插件    SM_BUILD_TEST_PLUGINS = ${SM_BUILD_TEST_PLUGINS}")
message(STATUS "  单元测试      SM_BUILD_TESTS      = ${SM_BUILD_TESTS}")
message(STATUS "  Win7 兼容     SM_WIN7_COMPAT      = ${SM_WIN7_COMPAT}")
message(STATUS "  内嵌中文字体  SM_EMBED_CJK_FONT   = ${SM_EMBED_CJK_FONT}")
message(STATUS "  私有平台层    SM_PLATFORM         = ${SM_PLATFORM}")
message(STATUS "  imgui 可用    ${SM_HAVE_IMGUI}")
message(STATUS "  toml++ 可用   ${SM_HAVE_TOMLPP}")
message(STATUS "  GLFW 可用     ${SM_HAVE_GLFW}")
message(STATUS "  curl 可用     ${SM_HAVE_CURL}")
message(STATUS "  httplib 可用  ${SM_HAVE_HTTPLIB}")

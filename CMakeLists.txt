cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SmVersion)
sm_detect_version("${CMAKE_CURRENT_SOURCE_DIR}")

project(SimpleMessage
        VERSION ${SM_VERSION}
        DESCRIPTION "基于插件机制的通知聚合与投递工具"
        LANGUAGES CXX)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# ---------------------------------------------------------------------------
# 运行期产物布局
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/lib")

include(SmOptions)
include(SmPlatform)

# ---------------------------------------------------------------------------
# sm_sdk —— SDK 公共头（纯头文件，零依赖）
# ---------------------------------------------------------------------------
add_library(sm_sdk INTERFACE)
add_library(sm::sdk ALIAS sm_sdk)
target_include_directories(sm_sdk INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(sm_sdk INTERFACE cxx_std_17)
# version.h 里的宏只是独立编译时的兜底值；正常构建由这里覆盖成 tag 推导值。
target_compile_definitions(sm_sdk INTERFACE
    SM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SM_VERSION_STRING="${SM_VERSION}"
    SM_VERSION_DETAIL="${SM_VERSION_DETAIL}")

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

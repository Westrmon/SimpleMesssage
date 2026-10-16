set(_sm_have_imgui FALSE)
set(_sm_have_tomlpp FALSE)
set(_sm_have_glfw FALSE)

# ---------------------------------------------------------------------------
# imgui
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp")
    add_library(sm_imgui SHARED
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
    )
    add_library(sm::imgui ALIAS sm_imgui)

    target_include_directories(sm_imgui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(sm_imgui
        PRIVATE   SM_IMGUI_BUILDING_DLL IMGUI_USER_CONFIG=\"sm_imgui_config.h\"
        INTERFACE IMGUI_USER_CONFIG=\"sm_imgui_config.h\")

    # 导出全部符号，否则插件看不到 ImGui:: 的内容。
    set_target_properties(sm_imgui PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF   # 用 IMGUI_API 显式控制，而不是全导出
        PREFIX "")

    # imgui 自身在 /W4 下有一批无害警告，且它是第三方代码——
    # 我们不为它改代码，也不让它的警告挡住自己的构建。
    if(MSVC)
        target_compile_options(sm_imgui PRIVATE /W3 /utf-8 /EHsc)
    endif()

    # 使用 imgui 的东西（宿主、notifier.imgui）需要 C++17 起步。
    target_compile_features(sm_imgui PUBLIC cxx_std_17)

    # Win7 兼容模式要求全程静态 CRT。sm_imgui 是唯一不走 sm_apply_* 的自家目标，
    # 不在这里跟着切的话它会单独用动态 CRT：跨 DLL 边界混两套 CRT（宿主 /MT、
    # 插件 /MT、imgui /MD），而且发布包里凭空多出一个"目标机器必须装 VC 运行时"
    # 的依赖 —— 现代版本的 redist 恰恰不支持 Win7，等于把 Win7 模式的意义抵消掉。
    if(SM_WIN7_COMPAT AND MSVC)
        set_property(TARGET sm_imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()

    set(_sm_have_imgui TRUE)

    if(WIN32)
        set(_sm_imgui_backend_sources
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_dx11.cpp")
    else()
        # POSIX 平台层当前只有空壳（见 docs/01-架构总览.md §6）。
        set(_sm_imgui_backend_sources "")
    endif()
else()
    message(WARNING
        "找不到 external/imgui/imgui.cpp —— 宿主与 notifier.imgui 插件将被跳过。"
        "这是随仓库提交的普通文件，缺失说明有人删过它；"
        "重新获取的方式见 external/PROVENANCE.txt 与 docs/02-宿主设计.md §20「依赖与构建」。")
endif()

# ---------------------------------------------------------------------------
# GLFW
# ---------------------------------------------------------------------------
# 一批文档目标"，或者"生成了一个没人部署的 glfw3.dll，宿主启动时报缺 DLL"。
# 把开关集中在这里，与 imgui 的理由相同：升级依赖时只改这一处。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/CMakeLists.txt")
    set(GLFW_LIBRARY_TYPE STATIC)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_BUILD_TESTS    OFF)
    set(GLFW_BUILD_DOCS     OFF)
    set(GLFW_INSTALL        OFF)
    if(SM_WIN7_COMPAT)
        set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF)
    else()
        set(USE_MSVC_RUNTIME_LIBRARY_DLL ON)
    endif()

    add_subdirectory(glfw)
    add_library(sm::glfw ALIAS glfw)
    if(WIN32)
        list(APPEND _sm_imgui_backend_sources
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp")
    endif()

    set(_sm_have_glfw TRUE)
else()
    message(WARNING
        "找不到 external/glfw/CMakeLists.txt —— imgui 的 GLFW/OpenGL3 后端不会"
        "被编译，宿主将缺少窗口层实现。这是随仓库提交的普通文件，缺失说明有人"
        "删过它；重新获取的方式见 external/PROVENANCE.txt。")
endif()

# ---------------------------------------------------------------------------
# toml++
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/toml.hpp")
    add_library(sm_tomlplusplus INTERFACE)
    add_library(sm::tomlplusplus ALIAS sm_tomlplusplus)
    target_include_directories(sm_tomlplusplus INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus")
    target_compile_features(sm_tomlplusplus INTERFACE cxx_std_17)
    set(_sm_have_tomlpp TRUE)
else()
    message(WARNING
        "找不到 external/tomlplusplus/toml.hpp —— 宿主将被跳过。"
        "这是随仓库提交的普通文件，缺失说明有人删过它；"
        "重新获取的方式见 external/PROVENANCE.txt 与 docs/02-宿主设计.md §20「依赖与构建」。")
endif()

# ---------------------------------------------------------------------------
# libcurl（source.email 的 IMAP/IMAPS 客户端）
# ---------------------------------------------------------------------------
set(_sm_have_curl FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
    set(BUILD_CURL_EXE    OFF)
    set(BUILD_STATIC_CURL OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_TESTING     OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_USE_LIBPSL   OFF)
    set(CURL_USE_LIBSSH2  OFF)
    set(CURL_USE_GSASL    OFF)
    set(CURL_ZLIB         OFF)
    set(CURL_BROTLI       OFF)
    set(CURL_ZSTD         OFF)
    set(CURL_USE_SCHANNEL ON)
    set(CURL_WINDOWS_SSPI ON)

    # curl 默认用动态 CRT 编静态库；Win7 模式下 source.email 是 /MT，
    # 混着链会得到一堆 LNK2001/LNK2019 的 __imp_fread / __imp_fwrite。
    # （curl 的 option() 会尊重这里的普通变量，见 CMP0077。）
    if(SM_WIN7_COMPAT)
        set(CURL_STATIC_CRT ON)
    endif()
    set(CURL_DISABLE_DICT       ON)
    set(CURL_DISABLE_DOH        ON)
    set(CURL_DISABLE_FILE       ON)
    set(CURL_DISABLE_FTP        ON)
    set(CURL_DISABLE_GOPHER     ON)
    set(CURL_DISABLE_IPFS       ON)
    set(CURL_DISABLE_LDAP       ON)
    set(CURL_DISABLE_MQTT       ON)
    set(CURL_DISABLE_POP3       ON)
    set(CURL_DISABLE_RTSP       ON)
    set(CURL_DISABLE_SMTP       ON)
    set(CURL_DISABLE_TELNET     ON)
    set(CURL_DISABLE_TFTP       ON)
    set(CURL_DISABLE_WEBSOCKETS ON)

    add_subdirectory(curl)
    add_library(sm::curl ALIAS libcurl_static)
    target_compile_features(libcurl_static INTERFACE cxx_std_17)

    set(_sm_have_curl TRUE)
else()
    message(WARNING
        "找不到 external/curl/CMakeLists.txt —— source.email 插件将被跳过。"
        "这是随仓库提交的普通文件树，缺失说明有人删过它；"
        "重新获取的方式见 external/PROVENANCE.txt。")
endif()

# ---------------------------------------------------------------------------
# cpp-httplib（source.webhook 的 HTTP 接收端）
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/httplib/httplib.h")
    add_library(sm_httplib INTERFACE)
    add_library(sm::httplib ALIAS sm_httplib)
    target_include_directories(sm_httplib INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/httplib")
    target_compile_features(sm_httplib INTERFACE cxx_std_17)
    if(WIN32)
        target_link_libraries(sm_httplib INTERFACE ws2_32)
    endif()
    set(_sm_have_httplib TRUE)
else()
    message(WARNING
        "找不到 external/httplib/httplib.h —— source.webhook 插件将被跳过。"
        "这是随仓库提交的普通文件，缺失说明有人删过它；"
        "重新获取的方式见 external/PROVENANCE.txt。")
endif()

# ---------------------------------------------------------------------------
# 对外发布依赖可用性
# ---------------------------------------------------------------------------
set(SM_HAVE_IMGUI  ${_sm_have_imgui}  PARENT_SCOPE)
set(SM_HAVE_TOMLPP ${_sm_have_tomlpp} PARENT_SCOPE)
set(SM_HAVE_GLFW   ${_sm_have_glfw}   PARENT_SCOPE)
set(SM_HAVE_CURL   ${_sm_have_curl}   PARENT_SCOPE)
set(SM_HAVE_HTTPLIB ${_sm_have_httplib} PARENT_SCOPE)
set(SM_IMGUI_BACKEND_SOURCES ${_sm_imgui_backend_sources} PARENT_SCOPE)

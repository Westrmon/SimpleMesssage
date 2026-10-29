set(_sm_have_imgui FALSE)
set(_sm_have_tomlpp FALSE)
set(_sm_have_glfw FALSE)
set(_sm_have_miniaudio FALSE)
set(_sm_have_wintoast FALSE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/miniaudio.h")
    add_library(sm_miniaudio STATIC miniaudio/miniaudio_impl.cpp)
    add_library(sm::miniaudio ALIAS sm_miniaudio)

    target_include_directories(sm_miniaudio PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio")
    target_compile_features(sm_miniaudio PUBLIC cxx_std_17)

    target_compile_definitions(sm_miniaudio PUBLIC
        MA_NO_ENCODING
        MA_NO_GENERATION
        MA_NO_FLAC
        MA_NO_VORBIS
        MA_NO_RESOURCE_MANAGER
        MA_NO_JACK
        MA_NO_SNDIO
        MA_NO_AUDIO4
        MA_NO_OSS
        MA_NO_AAUDIO
        MA_NO_OPENSL
        MA_NO_WEBAUDIO
        MA_NO_CUSTOM
        MA_NO_NULL)
    if(WIN32)
        target_compile_definitions(sm_miniaudio PUBLIC
            MA_NO_DSOUND
            MA_NO_WINMM)
    elseif(APPLE)
        target_compile_definitions(sm_miniaudio PUBLIC
            MA_NO_ALSA
            MA_NO_PULSEAUDIO)
    else()
        target_compile_definitions(sm_miniaudio PUBLIC
            MA_NO_COREAUDIO)
    endif()
    set_target_properties(sm_miniaudio PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

    if(MSVC)
        # C4244 是 miniaudio 内部的窄化转换，按第三方代码处理。
        # miniaudio 的全部实现都在一个 .obj 里，所以让每个函数 / 全局量各自
        # 成为 COMDAT（/Gy /Gw），再配合 /Zc:inline 丢弃未引用的内联函数，
        # 链接器就能把用不到的代码从插件和宿主体积里删掉。
        # /O1（最小体积）覆盖 Release 默认的 /O2：通知音只有几百 KB 的解码
        # 负载，这里换来的体积收益远大于那点性能差异。
        target_compile_options(sm_miniaudio PRIVATE
            /W3 /utf-8 /wd4244 /Gy /Gw /Zc:inline /O1)
        if(SM_WIN7_COMPAT)
            set_property(TARGET sm_miniaudio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        else()
            set_property(TARGET sm_miniaudio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # 与 MSVC 的 /O1 + COMDAT 对应：-Os 优先体积，每个函数 / 全局量单独
        # 成节，最后由链接器 --gc-sections（macOS 是 -dead_strip）丢掉未引用项。
        target_compile_options(sm_miniaudio PRIVATE
            -Os -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(sm_miniaudio INTERFACE -Wl,-dead_strip)
        elseif(UNIX)
            target_link_options(sm_miniaudio INTERFACE -Wl,--gc-sections)
        endif()
    endif()

    if(WIN32)
        target_link_libraries(sm_miniaudio PUBLIC ole32)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(sm_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()

    set(_sm_have_miniaudio TRUE)
else()
    message(WARNING
        "找不到 external/miniaudio/miniaudio.h —— notifier.sound 插件将被跳过。")
endif()

if(WIN32 AND NOT SM_WIN7_COMPAT
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/WinToast/src/wintoastlib.cpp")
    add_library(sm_wintoast STATIC WinToast/src/wintoastlib.cpp)
    add_library(sm::wintoast ALIAS sm_wintoast)

    target_include_directories(sm_wintoast PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/WinToast/include")
    target_compile_features(sm_wintoast PUBLIC cxx_std_17)
    target_link_libraries(sm_wintoast PUBLIC
        ole32 propsys psapi shlwapi user32)

    if(MSVC)
        target_compile_options(sm_wintoast PRIVATE /W3 /utf-8)
        set_property(TARGET sm_wintoast PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    endif()

    set(_sm_have_wintoast TRUE)
elseif(WIN32 AND SM_WIN7_COMPAT)
    message(STATUS
        "SM_WIN7_COMPAT=ON：跳过 WinToast 与 notifier.wintoast（原生 Toast 需要 Windows 8+）。")
elseif(WIN32)
    message(WARNING
        "找不到 external/WinToast/src/wintoastlib.cpp —— notifier.wintoast 插件将被跳过。")
endif()

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

    set_target_properties(sm_imgui PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF   # 用 IMGUI_API 显式控制，而不是全导出
        PREFIX "")

    if(MSVC)
        target_compile_options(sm_imgui PRIVATE /W3 /utf-8 /EHsc)
    endif()

    target_compile_features(sm_imgui PUBLIC cxx_std_17)

    if(SM_WIN7_COMPAT AND MSVC)
        set_property(TARGET sm_imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()

    set(_sm_have_imgui TRUE)

    if(WIN32)
        set(_sm_imgui_backend_sources
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_dx11.cpp")
    else()
        set(_sm_imgui_backend_sources "")
    endif()
else()
    message(WARNING
        "找不到 external/imgui/imgui.cpp —— 宿主与 notifier.imgui 插件将被跳过。")
endif()

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
        "找不到 external/glfw/CMakeLists.txt")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/toml.hpp")
    add_library(sm_tomlplusplus INTERFACE)
    add_library(sm::tomlplusplus ALIAS sm_tomlplusplus)
    target_include_directories(sm_tomlplusplus INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus")
    target_compile_features(sm_tomlplusplus INTERFACE cxx_std_17)
    set(_sm_have_tomlpp TRUE)
else()
    message(WARNING
        "找不到 external/tomlplusplus/toml.hpp —— 宿主将被跳过。")
endif()

set(_sm_have_curl FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
    set(BUILD_CURL_EXE    OFF)
    set(BUILD_STATIC_CURL OFF)
    set(BUILD_SHARED_LIBS ON)
    set(BUILD_STATIC_LIBS OFF)
    set(BUILD_EXAMPLES    OFF)
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
    set(CURL_DISABLE_WEBSOCKETS OFF)

    add_subdirectory(curl)
    add_library(sm::curl ALIAS libcurl_shared)
    set_target_properties(libcurl_shared PROPERTIES OUTPUT_NAME "sm_curl")
    target_compile_features(libcurl_shared INTERFACE cxx_std_17)

    set(_sm_have_curl TRUE)
else()
    message(WARNING
        "找不到 external/curl/CMakeLists.txt —— source.email 与 source.gotify 插件将被跳过。")
endif()

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
        "找不到 external/httplib/httplib.h —— source.webhook 插件将被跳过。")
endif()

set(SM_HAVE_MINIAUDIO ${_sm_have_miniaudio} PARENT_SCOPE)
set(SM_HAVE_WINTOAST ${_sm_have_wintoast} PARENT_SCOPE)
set(SM_HAVE_IMGUI  ${_sm_have_imgui}  PARENT_SCOPE)
set(SM_HAVE_TOMLPP ${_sm_have_tomlpp} PARENT_SCOPE)
set(SM_HAVE_GLFW   ${_sm_have_glfw}   PARENT_SCOPE)
set(SM_HAVE_CURL   ${_sm_have_curl}   PARENT_SCOPE)
set(SM_HAVE_HTTPLIB ${_sm_have_httplib} PARENT_SCOPE)
set(SM_IMGUI_BACKEND_SOURCES ${_sm_imgui_backend_sources} PARENT_SCOPE)

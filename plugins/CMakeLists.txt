# SimpleMessage — 插件目录

function(sm_configure_plugin target output_dir)
    add_library(${target} SHARED "${target}/plugin.cpp")

    target_link_libraries(${target} PRIVATE sm::sdk)

    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target}"
        RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${output_dir}")

    sm_apply_plugin_settings(${target})
endfunction()

function(sm_add_plugin target)
    sm_configure_plugin(${target} "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins")
endfunction()

function(sm_add_test_plugin target)
    sm_configure_plugin(${target} "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins_test")
endfunction()

if(SM_BUILD_SAMPLES)
    sm_add_plugin(notifier.console)

    sm_add_plugin(notifier.sound)
    if(WIN32)
        target_link_libraries(notifier.sound PRIVATE winmm)
    endif()

    if(SM_HAVE_CURL)
        sm_add_plugin(source.email)
        target_link_libraries(source.email PRIVATE sm::curl)

        sm_add_plugin(source.gotify)
        target_link_libraries(source.gotify PRIVATE sm::curl)
    else()
        message(STATUS "跳过 source.email 与 source.gotify：libcurl 不可用。")
    endif()

    if(SM_HAVE_HTTPLIB)
        sm_add_plugin(source.webhook)
        target_link_libraries(source.webhook PRIVATE sm::httplib)
    else()
        message(STATUS "跳过 source.webhook：cpp-httplib 不可用。")
    endif()

    if(SM_HAVE_IMGUI)
        sm_add_plugin(notifier.imgui)
        target_link_libraries(notifier.imgui PRIVATE sm::imgui)
    else()
        message(STATUS "跳过 notifier.imgui：imgui 不可用。")
    endif()

    install(DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins/" DESTINATION plugins
            OPTIONAL)
endif()

if(SM_BUILD_TEST_PLUGINS)
    add_subdirectory(_test)
    add_subdirectory(_badmeta)
endif()

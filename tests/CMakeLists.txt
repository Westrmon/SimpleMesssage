# SimpleMessage — 单元测试


add_executable(sm_sdk_tests
    main.cpp
    test_framework.cpp
    test_version_range.cpp
    test_summary.cpp
)

target_link_libraries(sm_sdk_tests PRIVATE sm::sdk)

sm_apply_platform_settings(sm_sdk_tests)

add_test(NAME sm_sdk_tests COMMAND sm_sdk_tests)

if(SM_HAVE_TOMLPP)
    add_executable(sm_host_rules_tests
        main.cpp
        test_framework.cpp
        test_rules.cpp
        test_rule_time.cpp
        test_placeholders.cpp
        "${CMAKE_SOURCE_DIR}/src/host/rules.cpp"
        "${CMAKE_SOURCE_DIR}/src/host/rule_time.cpp"
        "${CMAKE_SOURCE_DIR}/src/host/placeholders.cpp"
    )

    target_include_directories(sm_host_rules_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src")

    target_link_libraries(sm_host_rules_tests PRIVATE
        sm::sdk
        sm::tomlplusplus
    )

    sm_apply_platform_settings(sm_host_rules_tests)

    add_test(NAME sm_host_rules_tests COMMAND sm_host_rules_tests)

    add_executable(sm_host_instances_tests
        main.cpp
        test_framework.cpp
        test_instances.cpp
        "${CMAKE_SOURCE_DIR}/src/host/instances.cpp"
    )

    target_include_directories(sm_host_instances_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src")

    target_link_libraries(sm_host_instances_tests PRIVATE
        sm::sdk
        sm::tomlplusplus
    )

    sm_apply_platform_settings(sm_host_instances_tests)

    add_test(NAME sm_host_instances_tests COMMAND sm_host_instances_tests)
else()
    message(STATUS "跳过 sm_host_rules_tests / sm_host_instances_tests：toml++ 不可用")
endif()

add_executable(hub-catalog-test test_app_catalog.cpp)
target_link_libraries(hub-catalog-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-catalog-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-catalog-test COMMAND hub-catalog-test)

add_executable(hub-launcher-test test_app_launcher.cpp)
target_link_libraries(hub-launcher-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-launcher-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-launcher-test COMMAND hub-launcher-test)

add_executable(hub-logger-test test_hub_logger.cpp)
target_link_libraries(hub-logger-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-logger-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-logger-test COMMAND hub-logger-test)

if(WIN32)
    get_target_property(QT_HUB_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_HUB_CORE_DLL)
        get_target_property(QT_HUB_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_HUB_BIN_DIR "${QT_HUB_CORE_DLL}" DIRECTORY)
    find_program(QT_HUB_DEPLOY_TOOL windeployqt HINTS "${QT_HUB_BIN_DIR}" REQUIRED)

    foreach(TEST_TARGET IN ITEMS
            hub-catalog-test
            hub-launcher-test
            hub-logger-test)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_HUB_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

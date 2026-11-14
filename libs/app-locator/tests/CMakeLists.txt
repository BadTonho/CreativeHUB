add_executable(app-locator-test app_locator_test.cpp)
target_link_libraries(app-locator-test PRIVATE creative-suite::app-locator Qt6::Core)
target_compile_features(app-locator-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME app-locator-test COMMAND app-locator-test)

if(WIN32)
    get_target_property(QT_LOCATOR_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_LOCATOR_CORE_DLL)
        get_target_property(QT_LOCATOR_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_LOCATOR_BIN_DIR "${QT_LOCATOR_CORE_DLL}" DIRECTORY)
    find_program(QT_LOCATOR_DEPLOY_TOOL windeployqt HINTS "${QT_LOCATOR_BIN_DIR}" REQUIRED)
    add_custom_command(TARGET app-locator-test POST_BUILD
        COMMAND "${QT_LOCATOR_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:app-locator-test>"
        VERBATIM
    )
endif()

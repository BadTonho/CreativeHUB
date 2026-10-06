add_executable(updater-catalog-test release_catalog_test.cpp)
target_link_libraries(updater-catalog-test PRIVATE creative-suite::updater Qt6::Core)
target_compile_features(updater-catalog-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME updater-catalog-test COMMAND updater-catalog-test)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)
add_executable(updater-service-test update_service_test.cpp)
target_link_libraries(updater-service-test PRIVATE creative-suite::updater Qt6::Test)
target_compile_features(updater-service-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME updater-service-test COMMAND updater-service-test)

if(WIN32)
    get_target_property(QT_UPDATER_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_UPDATER_CORE_DLL)
        get_target_property(QT_UPDATER_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_UPDATER_BIN_DIR "${QT_UPDATER_CORE_DLL}" DIRECTORY)
    find_program(QT_UPDATER_DEPLOY_TOOL windeployqt HINTS "${QT_UPDATER_BIN_DIR}" REQUIRED)
    foreach(TEST_TARGET IN ITEMS updater-catalog-test updater-service-test)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_UPDATER_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

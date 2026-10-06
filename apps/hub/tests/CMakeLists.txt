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

add_executable(hub-card-widget-test test_app_card_widget.cpp)
target_link_libraries(hub-card-widget-test PRIVATE creative-suite-hub-core Qt6::Widgets)
target_compile_features(hub-card-widget-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-card-widget-test COMMAND hub-card-widget-test)

add_executable(hub-details-modal-test test_app_details_modal.cpp)
target_link_libraries(hub-details-modal-test PRIVATE creative-suite-hub-core Qt6::Widgets)
target_compile_features(hub-details-modal-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-details-modal-test COMMAND hub-details-modal-test)

add_executable(hub-changelog-reader-test test_changelog_reader.cpp)
target_link_libraries(hub-changelog-reader-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-changelog-reader-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-changelog-reader-test COMMAND hub-changelog-reader-test)

add_executable(hub-recent-projects-test test_recent_projects.cpp)
target_link_libraries(hub-recent-projects-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-recent-projects-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-recent-projects-test COMMAND hub-recent-projects-test)

add_executable(hub-storage-manager-test test_storage_manager.cpp)
target_link_libraries(hub-storage-manager-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-storage-manager-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-storage-manager-test COMMAND hub-storage-manager-test)

add_executable(hub-activity-manager-test test_activity_manager.cpp)
target_link_libraries(hub-activity-manager-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-activity-manager-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-activity-manager-test COMMAND hub-activity-manager-test)

add_executable(hub-backup-manager-test test_backup_manager.cpp)
target_link_libraries(hub-backup-manager-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-backup-manager-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-backup-manager-test COMMAND hub-backup-manager-test)

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
            hub-logger-test
            hub-card-widget-test
            hub-details-modal-test
            hub-changelog-reader-test
            hub-recent-projects-test
            hub-storage-manager-test
            hub-activity-manager-test
            hub-backup-manager-test)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_HUB_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

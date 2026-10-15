add_executable(hub-catalog-test test_app_catalog.cpp)
target_link_libraries(hub-catalog-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-catalog-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-catalog-test COMMAND hub-catalog-test)

add_executable(hub-launcher-test test_app_launcher.cpp)
target_link_libraries(hub-launcher-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-launcher-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-launcher-test COMMAND hub-launcher-test)

# Verify discovery against the enabled producers' actual executable outputs.
set(HUB_LAUNCHER_BUILD_ARGUMENTS)
foreach(EDITOR_TARGET IN ITEMS creative-suite-main-editor creative-suite-image-editor creative-suite-motion-editor)
    if(TARGET ${EDITOR_TARGET})
        if(EDITOR_TARGET STREQUAL "creative-suite-main-editor")
            set(EDITOR_ID video-editor)
        elseif(EDITOR_TARGET STREQUAL "creative-suite-image-editor")
            set(EDITOR_ID image-editor)
        else()
            set(EDITOR_ID motion-editor)
        endif()
        list(APPEND HUB_LAUNCHER_BUILD_ARGUMENTS "${EDITOR_ID}" "$<TARGET_FILE:${EDITOR_TARGET}>")
        add_dependencies(hub-launcher-test ${EDITOR_TARGET})
    endif()
endforeach()
if(HUB_LAUNCHER_BUILD_ARGUMENTS)
    add_dependencies(hub-launcher-test creative-suite-hub)
    add_creative_suite_test(NAME hub-launcher-build-layout-test COMMAND hub-launcher-test
        --built-apps "$<TARGET_FILE_DIR:creative-suite-hub>" ${HUB_LAUNCHER_BUILD_ARGUMENTS})
endif()

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

add_executable(hub-storage-page-test
    test_storage_page.cpp
    ../resources/translations.qrc
)
target_link_libraries(hub-storage-page-test PRIVATE creative-suite-hub-core Qt6::Widgets)
target_compile_features(hub-storage-page-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-storage-page-test COMMAND hub-storage-page-test)

add_executable(hub-activity-manager-test test_activity_manager.cpp)
target_link_libraries(hub-activity-manager-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-activity-manager-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-activity-manager-test COMMAND hub-activity-manager-test)

add_executable(hub-backup-manager-test test_backup_manager.cpp)
target_link_libraries(hub-backup-manager-test PRIVATE creative-suite-hub-core Qt6::Core)
target_compile_features(hub-backup-manager-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-backup-manager-test COMMAND hub-backup-manager-test)

add_executable(hub-backups-page-test
    test_backups_page.cpp
    ../resources/translations.qrc
)
target_link_libraries(hub-backups-page-test PRIVATE creative-suite-hub-core Qt6::Widgets)
target_compile_features(hub-backups-page-test PRIVATE cxx_std_20)
add_creative_suite_test(NAME hub-backups-page-test COMMAND hub-backups-page-test)

if(BUILD_IMAGE_EDITOR OR BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR)
    add_executable(hub-backup-format-test test_backup_formats.cpp)
    target_link_libraries(hub-backup-format-test PRIVATE creative-suite-hub-core)
    if(TARGET creative-suite-composition-exchange)
        target_link_libraries(hub-backup-format-test PRIVATE creative-suite::composition-exchange)
    endif()
    target_compile_features(hub-backup-format-test PRIVATE cxx_std_20)
    if(BUILD_IMAGE_EDITOR)
        target_link_libraries(hub-backup-format-test PRIVATE creative-suite-image-editor-core)
        target_compile_definitions(hub-backup-format-test PRIVATE HUB_BACKUP_IMAGE_CONTRACT)
    endif()
    if(BUILD_MOTION_EDITOR)
        target_link_libraries(hub-backup-format-test PRIVATE creative-suite-motion-editor-core)
        target_compile_definitions(hub-backup-format-test PRIVATE HUB_BACKUP_MOTION_CONTRACT)
    endif()
    if(BUILD_VIDEO_EDITOR)
        set(HUB_VIDEO_SOURCE_DIR "${PROJECT_SOURCE_DIR}/apps/video-editor/src")
        target_include_directories(hub-backup-format-test PRIVATE "${HUB_VIDEO_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/libs/motion-handoff/include")
        target_link_libraries(hub-backup-format-test PRIVATE creative-suite::media-assets)
        target_compile_definitions(hub-backup-format-test PRIVATE HUB_BACKUP_VIDEO_CONTRACT)
        target_sources(hub-backup-format-test PRIVATE
            ${HUB_VIDEO_SOURCE_DIR}/project/project_file.cpp
            ${HUB_VIDEO_SOURCE_DIR}/project/project_file_reader.cpp
            ${HUB_VIDEO_SOURCE_DIR}/project/project_file_writer.cpp
            ${HUB_VIDEO_SOURCE_DIR}/project/project_document_validator.cpp
            ${HUB_VIDEO_SOURCE_DIR}/workspaces/fusion/nodes/model/node_graph.cpp
            ${HUB_VIDEO_SOURCE_DIR}/timeline/timeline_transform.cpp)
    endif()
    creative_suite_deploy_media_runtime(hub-backup-format-test)
    add_creative_suite_test(NAME hub-backup-format-test COMMAND hub-backup-format-test)
endif()

if(WIN32)
    add_executable(hub-update-installation-refresh-test test_update_installation_refresh.cpp)
    target_link_libraries(hub-update-installation-refresh-test PRIVATE creative-suite-hub-core Qt6::Widgets)
    target_compile_features(hub-update-installation-refresh-test PRIVATE cxx_std_20)
    add_creative_suite_test(NAME hub-update-installation-refresh-test COMMAND hub-update-installation-refresh-test)
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
            hub-storage-page-test
            hub-activity-manager-test
            hub-backup-manager-test
            hub-backups-page-test)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_HUB_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
    add_custom_command(TARGET hub-update-installation-refresh-test POST_BUILD
        COMMAND "${QT_HUB_DEPLOY_TOOL}" --no-translations --no-compiler-runtime
            "$<TARGET_FILE:hub-update-installation-refresh-test>"
        VERBATIM)
    if(TARGET hub-backup-format-test)
        add_custom_command(TARGET hub-backup-format-test POST_BUILD
            COMMAND "${QT_HUB_DEPLOY_TOOL}" --no-translations --no-compiler-runtime
                "$<TARGET_FILE:hub-backup-format-test>"
            VERBATIM)
    endif()
endif()

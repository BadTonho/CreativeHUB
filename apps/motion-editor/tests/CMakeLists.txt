add_executable(creative-suite-motion-editor-document-tests
    composition_document_test.cpp
)
target_link_libraries(creative-suite-motion-editor-document-tests PRIVATE
    creative-suite-motion-editor-core
)
target_compile_features(creative-suite-motion-editor-document-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-document
    COMMAND creative-suite-motion-editor-document-tests)

add_executable(creative-suite-motion-editor-history-tests
    composition_history_test.cpp
)
target_link_libraries(creative-suite-motion-editor-history-tests PRIVATE
    creative-suite-motion-editor-core
)
target_compile_features(creative-suite-motion-editor-history-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-history
    COMMAND creative-suite-motion-editor-history-tests)

add_executable(creative-suite-motion-editor-persistence-tests
    motion_document_store_test.cpp
)
target_link_libraries(creative-suite-motion-editor-persistence-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Core
)
target_compile_features(creative-suite-motion-editor-persistence-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-persistence
    COMMAND creative-suite-motion-editor-persistence-tests)

add_executable(creative-suite-motion-editor-recovery-tests
    motion_recovery_store_test.cpp
)
target_link_libraries(creative-suite-motion-editor-recovery-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Core
)
target_compile_features(creative-suite-motion-editor-recovery-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-recovery
    COMMAND creative-suite-motion-editor-recovery-tests)

add_executable(creative-suite-motion-editor-preview-tests
    preview_renderer_test.cpp
)
target_link_libraries(creative-suite-motion-editor-preview-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Core
)
target_compile_features(creative-suite-motion-editor-preview-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-preview
    COMMAND creative-suite-motion-editor-preview-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-preview-tests)
set_tests_properties(creative-suite-motion-editor-preview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-motion-editor-export-tests
    motion_video_export_test.cpp
)
target_link_libraries(creative-suite-motion-editor-export-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-export-tests PRIVATE cxx_std_20)
target_compile_definitions(creative-suite-motion-editor-export-tests PRIVATE
    MOTION_EDITOR_EXPORT_TEST_MEDIA_DIR="${PROJECT_SOURCE_DIR}/prototypes/testdata"
)
add_creative_suite_test(
    NAME creative-suite-motion-editor-export
    COMMAND creative-suite-motion-editor-export-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-export-tests)
set_tests_properties(creative-suite-motion-editor-export PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-motion-editor-startup-tests
    motion_editor_startup_test.cpp
)
target_link_libraries(creative-suite-motion-editor-startup-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-startup-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-startup
    COMMAND creative-suite-motion-editor-startup-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-startup-tests)
set_tests_properties(creative-suite-motion-editor-startup PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-motion-editor-ui-tests
    motion_editor_ui_test.cpp
)
target_link_libraries(creative-suite-motion-editor-ui-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-ui-tests PRIVATE cxx_std_20)
target_compile_definitions(creative-suite-motion-editor-ui-tests PRIVATE
    MOTION_EDITOR_TEST_MEDIA_DIR="${PROJECT_SOURCE_DIR}/prototypes/testdata"
)
add_creative_suite_test(
    NAME creative-suite-motion-editor-ui
    COMMAND creative-suite-motion-editor-ui-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-ui-tests)
set_tests_properties(creative-suite-motion-editor-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(MSVC)
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-history-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-persistence-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-recovery-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-preview-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-export-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-history-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-persistence-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-recovery-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-preview-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-export-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_MOTION_TEST_CORE_DLL)
        get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_MOTION_TEST_BIN_DIR "${QT_MOTION_TEST_CORE_DLL}" DIRECTORY)
    find_program(QT_MOTION_TEST_DEPLOY_TOOL windeployqt HINTS "${QT_MOTION_TEST_BIN_DIR}" REQUIRED)

    foreach(TEST_TARGET IN ITEMS
            creative-suite-motion-editor-preview-tests
            creative-suite-motion-editor-export-tests
            creative-suite-motion-editor-startup-tests
            creative-suite-motion-editor-ui-tests)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_MOTION_TEST_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                --include-plugins qoffscreen
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

add_executable(creative-suite-motion-editor-document-tests
    composition_document_test.cpp
)

add_executable(creative-suite-motion-editor-hierarchy-tests layer_transform_hierarchy_test.cpp)
target_link_libraries(creative-suite-motion-editor-hierarchy-tests PRIVATE creative-suite-motion-editor-core Qt6::Widgets)
target_compile_features(creative-suite-motion-editor-hierarchy-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-motion-editor-hierarchy COMMAND creative-suite-motion-editor-hierarchy-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-hierarchy-tests)
set_tests_properties(creative-suite-motion-editor-hierarchy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

add_executable(creative-suite-motion-editor-anchor-tests layer_anchor_test.cpp)
target_link_libraries(creative-suite-motion-editor-anchor-tests PRIVATE creative-suite-motion-editor-core Qt6::Widgets)
target_compile_features(creative-suite-motion-editor-anchor-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-motion-editor-anchor COMMAND creative-suite-motion-editor-anchor-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-anchor-tests)
set_tests_properties(creative-suite-motion-editor-anchor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

add_executable(creative-suite-motion-editor-scale-tests layer_scale_test.cpp)
target_link_libraries(creative-suite-motion-editor-scale-tests PRIVATE creative-suite-motion-editor-core Qt6::Widgets)
target_compile_features(creative-suite-motion-editor-scale-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-motion-editor-scale COMMAND creative-suite-motion-editor-scale-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-scale-tests)
set_tests_properties(creative-suite-motion-editor-scale PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

add_executable(creative-suite-motion-editor-pan-behind-tests pan_behind_test.cpp)
target_link_libraries(creative-suite-motion-editor-pan-behind-tests PRIVATE creative-suite-motion-editor-core Qt6::Widgets)
target_compile_features(creative-suite-motion-editor-pan-behind-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-motion-editor-pan-behind COMMAND creative-suite-motion-editor-pan-behind-tests)
add_creative_suite_test(NAME creative-suite-motion-editor-pan-behind-hidpi COMMAND creative-suite-motion-editor-pan-behind-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-pan-behind-tests)
set_tests_properties(creative-suite-motion-editor-pan-behind PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1" TIMEOUT 90)
set_tests_properties(creative-suite-motion-editor-pan-behind-hidpi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2" TIMEOUT 90)
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
target_compile_definitions(creative-suite-motion-editor-preview-tests PRIVATE
    MOTION_EDITOR_TEST_MEDIA_DIR="${PROJECT_SOURCE_DIR}/prototypes/testdata"
)
add_creative_suite_test(
    NAME creative-suite-motion-editor-preview
    COMMAND creative-suite-motion-editor-preview-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-preview-tests)
set_tests_properties(creative-suite-motion-editor-preview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)

add_executable(creative-suite-motion-editor-gpu-composition-tests
    gpu_composition_test.cpp
)
target_link_libraries(creative-suite-motion-editor-gpu-composition-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Gui
)
qt_import_plugins(creative-suite-motion-editor-gpu-composition-tests
    INCLUDE Qt6::QOffscreenIntegrationPlugin)
target_compile_features(creative-suite-motion-editor-gpu-composition-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-gpu-composition
    COMMAND creative-suite-motion-editor-gpu-composition-tests)
set_tests_properties(creative-suite-motion-editor-gpu-composition PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)

add_executable(creative-suite-motion-editor-performance-tests
    performance_metrics_test.cpp
)
target_link_libraries(creative-suite-motion-editor-performance-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
qt_import_plugins(creative-suite-motion-editor-performance-tests
    INCLUDE Qt6::QOffscreenIntegrationPlugin)
target_compile_features(creative-suite-motion-editor-performance-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-performance
    COMMAND creative-suite-motion-editor-performance-tests)
set_tests_properties(creative-suite-motion-editor-performance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(creative-suite-motion-editor-export-tests
    motion_video_export_test.cpp
)
target_link_libraries(creative-suite-motion-editor-export-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
qt_import_plugins(creative-suite-motion-editor-export-tests
    INCLUDE Qt6::QOffscreenIntegrationPlugin)
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

add_executable(creative-suite-motion-editor-translation-tests
    motion_editor_translation_test.cpp
    ../resources/translations.qrc
)
target_link_libraries(creative-suite-motion-editor-translation-tests PRIVATE
    creative-suite-motion-editor-core
    creative-suite::translations
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-translation-tests PRIVATE cxx_std_20)
add_dependencies(creative-suite-motion-editor-translation-tests
    creative-suite-motion-editor-translation-catalog)
add_creative_suite_test(
    NAME creative-suite-motion-editor-translation-catalog
    COMMAND creative-suite-motion-editor-translation-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-translation-tests)
set_tests_properties(creative-suite-motion-editor-translation-catalog PROPERTIES
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

add_executable(creative-suite-motion-editor-audio-keyframes-tests
    audio_keyframe_generation_test.cpp
)
target_link_libraries(creative-suite-motion-editor-audio-keyframes-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-audio-keyframes-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-motion-editor-audio-keyframes
    COMMAND creative-suite-motion-editor-audio-keyframes-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-audio-keyframes-tests)
set_tests_properties(creative-suite-motion-editor-audio-keyframes PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)

# Measurement tool is explicitly invoked; it is not a timing-sensitive CTest.
add_executable(creative-suite-motion-editor-gpu-preview-benchmark gpu_preview_benchmark.cpp)
target_link_libraries(creative-suite-motion-editor-gpu-preview-benchmark PRIVATE creative-suite-motion-editor-core)
target_compile_features(creative-suite-motion-editor-gpu-preview-benchmark PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-gpu-preview-benchmark)

add_executable(creative-suite-motion-editor-gpu-texture-tests gpu_texture_preview_test.cpp)
target_link_libraries(creative-suite-motion-editor-gpu-texture-tests PRIVATE creative-suite-motion-editor-core)
target_compile_features(creative-suite-motion-editor-gpu-texture-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-motion-editor-gpu-texture
    COMMAND creative-suite-motion-editor-gpu-texture-tests)
creative_suite_deploy_media_runtime(creative-suite-motion-editor-gpu-texture-tests)
set_tests_properties(creative-suite-motion-editor-gpu-texture PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

if(MSVC)
    target_compile_options(creative-suite-motion-editor-pan-behind-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-scale-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-gpu-texture-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-history-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-persistence-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-recovery-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-preview-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-gpu-composition-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-performance-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-export-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-translation-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-audio-keyframes-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-motion-editor-pan-behind-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-scale-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-gpu-texture-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-history-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-persistence-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-recovery-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-preview-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-gpu-composition-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-performance-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-export-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-translation-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-audio-keyframes-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_MOTION_TEST_CORE_DLL)
        get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_MOTION_TEST_BIN_DIR "${QT_MOTION_TEST_CORE_DLL}" DIRECTORY)
    find_program(QT_MOTION_TEST_DEPLOY_TOOL windeployqt HINTS "${QT_MOTION_TEST_BIN_DIR}" REQUIRED)

    foreach(TEST_TARGET IN ITEMS
            creative-suite-motion-editor-pan-behind-tests
            creative-suite-motion-editor-scale-tests
            creative-suite-motion-editor-gpu-preview-benchmark
            creative-suite-motion-editor-gpu-texture-tests
            creative-suite-motion-editor-preview-tests
            creative-suite-motion-editor-gpu-composition-tests
            creative-suite-motion-editor-performance-tests
            creative-suite-motion-editor-export-tests
            creative-suite-motion-editor-startup-tests
            creative-suite-motion-editor-translation-tests
            creative-suite-motion-editor-audio-keyframes-tests
            creative-suite-motion-editor-ui-tests)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_MOTION_TEST_DEPLOY_TOOL}"
                --qtpaths "${QT_MOTION_TEST_BIN_DIR}/qtpaths.exe"
                --no-translations
                --no-compiler-runtime
                --include-plugins qoffscreen
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

add_executable(creative-suite-main-editor-transform-compositor-tests
    transform_compositor_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-transform-compositor-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-transform-compositor-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-transform-compositor-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-transform-compositor-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-transform-compositor
    COMMAND creative-suite-main-editor-transform-compositor-tests
)

add_executable(creative-suite-main-editor-text-compositor-tests
    text_compositor_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/text_renderer.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/text_renderer.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-text-compositor-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-text-compositor-tests PRIVATE
    Qt6::Gui
)

target_compile_features(creative-suite-main-editor-text-compositor-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-text-compositor-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-text-compositor-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FALSE)
    get_target_property(QT_TEXT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_TEXT_CORE_DLL)
        get_target_property(QT_TEXT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_TEXT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_TEXT_GUI_DLL)
        get_target_property(QT_TEXT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_TEXT_BIN_DIR "\${QT_TEXT_CORE_DLL}" DIRECTORY)
    find_program(QT_TEXT_DEPLOY_TOOL windeployqt HINTS "\${QT_TEXT_BIN_DIR}" REQUIRED)
    add_custom_command(TARGET creative-suite-main-editor-text-compositor-tests POST_BUILD
        COMMAND "\${CMAKE_COMMAND}" -E copy_if_different
            "\${QT_TEXT_CORE_DLL}"
            "\${QT_TEXT_GUI_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-text-compositor-tests>"
        COMMAND "\${QT_TEXT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-text-compositor-tests>"
        COMMAND "\${CMAKE_COMMAND}" -E copy_if_different
            "\${QT_TEXT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-text-compositor-tests>/platforms"
        VERBATIM
    )
endif()

deploy_qt_test_runtime(creative-suite-main-editor-text-compositor-tests)

add_test(
    NAME creative-suite-main-editor-text-compositor
    COMMAND creative-suite-main-editor-text-compositor-tests
)
set_tests_properties(creative-suite-main-editor-text-compositor PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-preview-performance-metrics-tests
    preview_performance_metrics_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE
    Threads::Threads
)

target_compile_features(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-preview-performance-metrics
    COMMAND creative-suite-main-editor-preview-performance-metrics-tests
)

preserve_video_editor_test_working_directory()

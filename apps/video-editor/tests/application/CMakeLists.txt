add_executable(creative-suite-main-editor-main-window-tests
    main_window_integration_test.cpp
)

target_link_libraries(creative-suite-main-editor-main-window-tests PRIVATE
    creative-suite-main-editor-core
)

target_compile_features(creative-suite-main-editor-main-window-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-main-window-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-main-window-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-main-window-tests)
deploy_qt_test_runtime(creative-suite-main-editor-main-window-tests)

add_test(
    NAME creative-suite-main-editor-main-window
    COMMAND creative-suite-main-editor-main-window-tests
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv"
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference-second.mkv"
)
set_tests_properties(creative-suite-main-editor-main-window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1"
)

add_executable(creative-suite-main-editor-application-media-tests
    application_media_services_test.cpp
)
target_link_libraries(creative-suite-main-editor-application-media-tests PRIVATE
    creative-suite-main-editor-core
)
target_compile_features(creative-suite-main-editor-application-media-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-main-editor-application-media-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-application-media-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
copy_ffmpeg_test_runtime(creative-suite-main-editor-application-media-tests)
deploy_qt_test_runtime(creative-suite-main-editor-application-media-tests)
add_test(
    NAME creative-suite-main-editor-application-media
    COMMAND creative-suite-main-editor-application-media-tests
)

preserve_video_editor_test_working_directory()

add_executable(creative-suite-main-editor-main-window-tests
    main_window_integration_test.cpp
)

target_link_libraries(creative-suite-main-editor-main-window-tests PRIVATE
    creative-suite-main-editor-core
)

target_compile_features(creative-suite-main-editor-main-window-tests PRIVATE cxx_std_20)
if(BUILD_IMAGE_EDITOR)
    target_link_libraries(creative-suite-main-editor-main-window-tests PRIVATE
        creative-suite-image-editor-core)
    target_compile_definitions(creative-suite-main-editor-main-window-tests PRIVATE
        CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS=1)
endif()

if(MSVC)
    target_compile_options(creative-suite-main-editor-main-window-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-main-window-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-main-window-tests)
deploy_qt_test_runtime(creative-suite-main-editor-main-window-tests)

add_creative_suite_test(
    NAME creative-suite-main-editor-main-window
    COMMAND creative-suite-main-editor-main-window-tests
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv"
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference-second.mkv"
)
set_tests_properties(creative-suite-main-editor-main-window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1"
)
add_creative_suite_test(
    NAME creative-suite-main-editor-project-settings-ui
    COMMAND creative-suite-main-editor-main-window-tests
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv"
        "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference-second.mkv"
        --project-settings
)
set_tests_properties(creative-suite-main-editor-project-settings-ui PROPERTIES
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
add_creative_suite_test(
    NAME creative-suite-main-editor-application-media
    COMMAND creative-suite-main-editor-application-media-tests
)

preserve_video_editor_test_working_directory()

if(BUILD_MOTION_EDITOR)
    add_executable(creative-suite-blur-interchange-tests blur_interchange_test.cpp)
    target_link_libraries(creative-suite-blur-interchange-tests PRIVATE creative-suite-main-editor-core creative-suite-motion-editor-core)
    copy_ffmpeg_test_runtime(creative-suite-blur-interchange-tests)
    deploy_qt_test_runtime(creative-suite-blur-interchange-tests)
    add_creative_suite_test(NAME creative-suite-blur-interchange COMMAND creative-suite-blur-interchange-tests "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv")
    set_tests_properties(creative-suite-blur-interchange PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1")
    if(WIN32)
        set_tests_properties(creative-suite-blur-interchange PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1")
    endif()

    add_executable(creative-suite-mask-interchange-tests mask_interchange_test.cpp)
    target_link_libraries(creative-suite-mask-interchange-tests PRIVATE creative-suite-main-editor-core creative-suite-motion-editor-core)
    copy_ffmpeg_test_runtime(creative-suite-mask-interchange-tests)
    deploy_qt_test_runtime(creative-suite-mask-interchange-tests)
    add_creative_suite_test(NAME creative-suite-mask-interchange COMMAND creative-suite-mask-interchange-tests)
    set_tests_properties(creative-suite-mask-interchange PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1")
    if(WIN32)
        set_tests_properties(creative-suite-mask-interchange PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1")
    endif()
    add_executable(creative-suite-composition-interchange-tests composition_interchange_test.cpp)
    target_link_libraries(creative-suite-composition-interchange-tests PRIVATE
        creative-suite-main-editor-core creative-suite-motion-editor-core)
    copy_ffmpeg_test_runtime(creative-suite-composition-interchange-tests)
    deploy_qt_test_runtime(creative-suite-composition-interchange-tests)
    add_creative_suite_test(NAME creative-suite-composition-interchange
        COMMAND creative-suite-composition-interchange-tests
            "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv")
    set_tests_properties(creative-suite-composition-interchange PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1")
    if(WIN32)
        set_tests_properties(creative-suite-composition-interchange PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1")
    endif()
endif()

add_creative_suite_test(NAME creative-suite-main-editor-linked-image
    COMMAND creative-suite-main-editor-main-window-tests --linked-image-only)
set_tests_properties(creative-suite-main-editor-linked-image PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1")

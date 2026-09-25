add_executable(creative-suite-main-editor-video-decoder-tests
    video_decoder_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_decoder.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_decoder.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/still_image_decoder.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/still_image_decoder.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_probe.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_probe.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-video-decoder-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor-video-decoder-tests PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(creative-suite-main-editor-video-decoder-tests PRIVATE
    Qt6::Gui
    ${FFMPEG_LIBRARIES}
)

target_compile_features(creative-suite-main-editor-video-decoder-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-video-decoder-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-video-decoder-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()



copy_ffmpeg_test_runtime(creative-suite-main-editor-video-decoder-tests)
deploy_qt_test_runtime(creative-suite-main-editor-video-decoder-tests)

add_test(
    NAME creative-suite-main-editor-video-decoder-errors
    COMMAND creative-suite-main-editor-video-decoder-tests
)

if(EXISTS "${REFERENCE_VIDEO}")
    add_test(
        NAME creative-suite-main-editor-video-decoder-reference
        COMMAND creative-suite-main-editor-video-decoder-tests "${REFERENCE_VIDEO}" 640 360
    )
endif()

add_executable(creative-suite-main-editor-media-library-tests
    media_library_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
)

target_include_directories(creative-suite-main-editor-media-library-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-media-library-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-media-library-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-media-library-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-media-library
    COMMAND creative-suite-main-editor-media-library-tests
)

preserve_video_editor_test_working_directory()

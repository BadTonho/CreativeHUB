add_executable(creative-suite-main-editor-video-playback-tests
    video_playback_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-video-playback-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor-video-playback-tests PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(creative-suite-main-editor-video-playback-tests PRIVATE
    ${FFMPEG_LIBRARIES}
)

target_compile_features(creative-suite-main-editor-video-playback-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-video-playback-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-video-playback-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-video-playback-tests)

add_test(
    NAME creative-suite-main-editor-video-playback-errors
    COMMAND creative-suite-main-editor-video-playback-tests
)

if(EXISTS "${REFERENCE_VIDEO}")
add_test(
    NAME creative-suite-main-editor-video-playback-reference
    COMMAND creative-suite-main-editor-video-playback-tests "${REFERENCE_VIDEO}"
)
endif()

add_executable(creative-suite-main-editor-playback-worker-tests
    playback_worker_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_playback.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_playback.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_playback.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/audio_output.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/audio_output.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_worker.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_worker.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_transition_plan.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_transition_plan.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_audio_pacing.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_deadline_scheduler.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_frame_mailbox.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_frame_mailbox.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/frame_compositor.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/text_renderer.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/text_renderer.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-playback-worker-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor-playback-worker-tests PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(creative-suite-main-editor-playback-worker-tests PRIVATE
    Qt6::Core
    Qt6::Gui
    ${FFMPEG_LIBRARIES}
)

if(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    target_link_libraries(creative-suite-main-editor-playback-worker-tests PRIVATE Qt6::Multimedia)
    target_compile_definitions(creative-suite-main-editor-playback-worker-tests PRIVATE CREATIVE_SUITE_HAS_QT_MULTIMEDIA=1)
endif()

target_compile_features(creative-suite-main-editor-playback-worker-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-playback-worker-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-playback-worker-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(creative-suite-main-editor-audio-playback-tests
    audio_playback_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_frame.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_playback.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_playback.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/audio_output.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/audio_output.h
)

target_include_directories(creative-suite-main-editor-audio-playback-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor-audio-playback-tests PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(creative-suite-main-editor-audio-playback-tests PRIVATE
    Qt6::Core
    ${FFMPEG_LIBRARIES}
)

if(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    target_link_libraries(creative-suite-main-editor-audio-playback-tests PRIVATE Qt6::Multimedia)
    target_compile_definitions(creative-suite-main-editor-audio-playback-tests PRIVATE CREATIVE_SUITE_HAS_QT_MULTIMEDIA=1)
endif()

target_compile_features(creative-suite-main-editor-audio-playback-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-audio-playback-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-audio-playback-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-audio-playback-tests)
deploy_qt_test_runtime(creative-suite-main-editor-audio-playback-tests)

add_test(
    NAME creative-suite-main-editor-audio-playback
    COMMAND creative-suite-main-editor-audio-playback-tests
)

if(EXISTS "${REFERENCE_VIDEO}")
    add_test(
        NAME creative-suite-main-editor-audio-playback-reference
        COMMAND creative-suite-main-editor-audio-playback-tests "${REFERENCE_VIDEO}"
    )
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-playback-worker-tests)
deploy_qt_test_runtime(creative-suite-main-editor-playback-worker-tests)

add_test(
    NAME creative-suite-main-editor-playback-worker-errors
    COMMAND creative-suite-main-editor-playback-worker-tests
)
set_tests_properties(creative-suite-main-editor-playback-worker-errors PROPERTIES
    ENVIRONMENT "CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-playback-controller-tests
    playback_controller_test.cpp
)

target_link_libraries(creative-suite-main-editor-playback-controller-tests PRIVATE
    creative-suite-main-editor-core
)

target_compile_features(creative-suite-main-editor-playback-controller-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-playback-controller-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-playback-controller-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-playback-controller-tests)
deploy_qt_test_runtime(creative-suite-main-editor-playback-controller-tests)

add_test(
    NAME creative-suite-main-editor-playback-controller
    COMMAND creative-suite-main-editor-playback-controller-tests
)
set_tests_properties(creative-suite-main-editor-playback-controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1"
)

if(EXISTS "${REFERENCE_VIDEO}")
    add_test(
        NAME creative-suite-main-editor-playback-worker-reference
        COMMAND creative-suite-main-editor-playback-worker-tests "${REFERENCE_VIDEO}"
    )
    set_tests_properties(creative-suite-main-editor-playback-worker-reference PROPERTIES
        ENVIRONMENT "CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1;QT_QPA_PLATFORM=offscreen"
    )
endif()

add_executable(creative-suite-main-editor-playback-deadline-tests
    playback_deadline_scheduler_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_audio_pacing.h
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/playback_deadline_scheduler.h
)

target_include_directories(creative-suite-main-editor-playback-deadline-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-playback-deadline-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-playback-deadline-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-playback-deadline-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-playback-deadline
    COMMAND creative-suite-main-editor-playback-deadline-tests
)

add_executable(creative-suite-main-editor-frame-step-navigation-tests
    frame_step_navigation_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/frame_step_navigation.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/playback/frame_step_navigation.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-frame-step-navigation-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-frame-step-navigation-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-frame-step-navigation-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-frame-step-navigation-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-frame-step-navigation
    COMMAND creative-suite-main-editor-frame-step-navigation-tests
)

preserve_video_editor_test_working_directory()

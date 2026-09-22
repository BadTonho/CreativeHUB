add_executable(creative-suite-main-editor-logger-tests
    logger_test.cpp
    ../src/logging/logger.cpp
    ../src/logging/logger.h
)

target_include_directories(creative-suite-main-editor-logger-tests PRIVATE
    ../src
)

target_compile_features(creative-suite-main-editor-logger-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-logger-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-logger-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-logger
    COMMAND creative-suite-main-editor-logger-tests
)

add_executable(creative-suite-main-editor-video-decoder-tests
    video_decoder_test.cpp
    ../src/logging/logger.cpp
    ../src/logging/logger.h
    ../src/media/video_decoder.cpp
    ../src/media/video_decoder.h
    ../src/media/video_frame.h
    ../src/media/video_metadata.h
    ../src/media/video_probe.cpp
    ../src/media/video_probe.h
    ../src/media/video_playback.cpp
    ../src/media/video_playback.h
    ../src/rendering/preview_performance_metrics.cpp
    ../src/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-video-decoder-tests PRIVATE
    ../src
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor-video-decoder-tests PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(creative-suite-main-editor-video-decoder-tests PRIVATE
    ${FFMPEG_LIBRARIES}
)

target_compile_features(creative-suite-main-editor-video-decoder-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-video-decoder-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-video-decoder-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(copy_ffmpeg_test_runtime target_name)
    if(WIN32)
        set(FFMPEG_TEST_RUNTIME_DLLS)
        foreach(FFMPEG_LIBRARY_DIR IN LISTS FFMPEG_LIBRARY_DIRS)
            if(FFMPEG_LIBRARY_DIR MATCHES "[/\\\\]debug([/\\\\]|$)")
                continue()
            endif()
            get_filename_component(FFMPEG_ROOT_DIR "${FFMPEG_LIBRARY_DIR}" DIRECTORY)
            foreach(FFMPEG_RUNTIME_NAME IN ITEMS avcodec avdevice avfilter avformat avutil swresample swscale)
                file(GLOB FFMPEG_RUNTIME_MATCHES
                    "${FFMPEG_ROOT_DIR}/bin/${FFMPEG_RUNTIME_NAME}-*.dll")
                list(APPEND FFMPEG_TEST_RUNTIME_DLLS ${FFMPEG_RUNTIME_MATCHES})
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES FFMPEG_TEST_RUNTIME_DLLS)

        if(FFMPEG_TEST_RUNTIME_DLLS)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${FFMPEG_TEST_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM
            )
        endif()
    endif()
endfunction()

function(deploy_qt_test_runtime target_name)
    if(WIN32 AND CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT QT_CORE_DLL)
            get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
        endif()
        get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
        find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${QT_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${target_name}>"
            VERBATIM
        )
    endif()
endfunction()

copy_ffmpeg_test_runtime(creative-suite-main-editor-video-decoder-tests)

add_test(
    NAME creative-suite-main-editor-video-decoder-errors
    COMMAND creative-suite-main-editor-video-decoder-tests
)

set(REFERENCE_VIDEO "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv")
if(EXISTS "${REFERENCE_VIDEO}")
    add_test(
        NAME creative-suite-main-editor-video-decoder-reference
        COMMAND creative-suite-main-editor-video-decoder-tests "${REFERENCE_VIDEO}" 640 360
    )
endif()

add_executable(creative-suite-main-editor-video-playback-tests
    video_playback_test.cpp
    ../src/logging/logger.cpp
    ../src/logging/logger.h
    ../src/media/video_frame.h
    ../src/media/video_metadata.h
    ../src/media/video_playback.cpp
    ../src/media/video_playback.h
    ../src/rendering/preview_performance_metrics.cpp
    ../src/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-video-playback-tests PRIVATE
    ../src
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
    ../src/logging/logger.cpp
    ../src/logging/logger.h
    ../src/media/video_frame.h
    ../src/media/video_metadata.h
    ../src/media/video_playback.cpp
    ../src/media/video_playback.h
    ../src/media/audio_frame.h
    ../src/media/audio_playback.cpp
    ../src/media/audio_playback.h
    ../src/playback/audio_output.cpp
    ../src/playback/audio_output.h
    ../src/playback/playback_worker.cpp
    ../src/playback/playback_worker.h
    ../src/playback/playback_audio_pacing.h
    ../src/playback/playback_deadline_scheduler.h
    ../src/playback/playback_frame_mailbox.cpp
    ../src/playback/playback_frame_mailbox.h
    ../src/rendering/frame_compositor.cpp
    ../src/rendering/frame_compositor.h
    ../src/rendering/preview_performance_metrics.cpp
    ../src/rendering/preview_performance_metrics.h
    ../src/rendering/text_renderer.cpp
    ../src/rendering/text_renderer.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-playback-worker-tests PRIVATE
    ../src
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
    ../src/logging/logger.cpp
    ../src/logging/logger.h
    ../src/media/audio_frame.h
    ../src/media/audio_playback.cpp
    ../src/media/audio_playback.h
    ../src/media/video_metadata.h
    ../src/playback/audio_output.cpp
    ../src/playback/audio_output.h
)

target_include_directories(creative-suite-main-editor-audio-playback-tests PRIVATE
    ../src
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
    ../src/playback/playback_audio_pacing.h
    ../src/playback/playback_deadline_scheduler.h
)

target_include_directories(creative-suite-main-editor-playback-deadline-tests PRIVATE
    ../src
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

add_executable(creative-suite-main-editor-timeline-tests
    timeline_model_test.cpp
    ../src/media/video_metadata.h
    ../src/timeline/timeline_history.cpp
    ../src/timeline/timeline_history.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-tests PRIVATE
    ../src
)

target_compile_features(creative-suite-main-editor-timeline-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-timeline
    COMMAND creative-suite-main-editor-timeline-tests
)

add_executable(creative-suite-main-editor-transform-compositor-tests
    transform_compositor_test.cpp
    ../src/media/video_frame.h
    ../src/media/video_metadata.h
    ../src/rendering/frame_compositor.cpp
    ../src/rendering/frame_compositor.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-transform-compositor-tests PRIVATE
    ../src
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
    ../src/media/video_frame.h
    ../src/rendering/frame_compositor.cpp
    ../src/rendering/frame_compositor.h
    ../src/rendering/text_renderer.cpp
    ../src/rendering/text_renderer.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-text-compositor-tests PRIVATE
    ../src
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

add_executable(creative-suite-main-editor-timeline-widget-tests
    timeline_widget_test.cpp
    ../src/media/video_metadata.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
    ../src/timeline/timeline_widget.cpp
    ../src/timeline/timeline_widget.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-widget-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-timeline-widget-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-timeline-widget-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-widget-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-widget-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-timeline-widget-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-timeline-widget-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-timeline-widget-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-timeline-widget-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-timeline-widget
    COMMAND creative-suite-main-editor-timeline-widget-tests
)
set_tests_properties(creative-suite-main-editor-timeline-widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-effects-panel-tests
    effects_panel_test.cpp
    ../src/ui/effects_catalog.cpp
    ../src/ui/effects_catalog.h
    ../src/ui/effects_favorites_widget.cpp
    ../src/ui/effects_favorites_widget.h
    ../src/ui/effects_toolbox_widget.cpp
    ../src/ui/effects_toolbox_widget.h
    ../src/ui/effects_list_widget.cpp
    ../src/ui/effects_list_widget.h
)

target_include_directories(creative-suite-main-editor-effects-panel-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-effects-panel-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-effects-panel-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-effects-panel-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-effects-panel-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

deploy_qt_test_runtime(creative-suite-main-editor-effects-panel-tests)

add_test(
    NAME creative-suite-main-editor-effects-panel
    COMMAND creative-suite-main-editor-effects-panel-tests
)
set_tests_properties(creative-suite-main-editor-effects-panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-media-browser-list-widget-tests
    media_browser_list_widget_test.cpp
    ../src/ui/media_browser_list_widget.cpp
    ../src/ui/media_browser_list_widget.h
    ../src/ui/media_drag_mime.h
)

target_include_directories(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-media-browser-list-widget-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-list-widget-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-media-browser-list-widget-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-list-widget-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-media-browser-list-widget
    COMMAND creative-suite-main-editor-media-browser-list-widget-tests
)
set_tests_properties(creative-suite-main-editor-media-browser-list-widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-media-browser-bin-tree-tests
    media_browser_bin_tree_widget_test.cpp
    ../src/ui/media_browser_bin_tree_widget.cpp
    ../src/ui/media_browser_bin_tree_widget.h
    ../src/ui/media_drag_mime.h
)

target_include_directories(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-media-browser-bin-tree-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-bin-tree-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-media-browser-bin-tree-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-bin-tree-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-media-browser-bin-tree
    COMMAND creative-suite-main-editor-media-browser-bin-tree-tests
)
set_tests_properties(creative-suite-main-editor-media-browser-bin-tree PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-settings-dialog-tests
    settings_dialog_test.cpp
    ../src/settings/settings_dialog.cpp
    ../src/settings/settings_dialog.h
    ../src/settings/shortcut_manager.cpp
    ../src/settings/shortcut_manager.h
    ../src/settings/user_preferences.cpp
    ../src/settings/user_preferences.h
)

target_include_directories(creative-suite-main-editor-settings-dialog-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-settings-dialog-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-settings-dialog-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-settings-dialog-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-settings-dialog-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-settings-dialog-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-settings-dialog-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-settings-dialog-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-settings-dialog-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-settings-dialog
    COMMAND creative-suite-main-editor-settings-dialog-tests
)
set_tests_properties(creative-suite-main-editor-settings-dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-shortcut-manager-tests
    shortcut_manager_test.cpp
    ../src/settings/shortcut_manager.cpp
    ../src/settings/shortcut_manager.h
)

target_include_directories(creative-suite-main-editor-shortcut-manager-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-shortcut-manager-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-shortcut-manager-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-shortcut-manager-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-shortcut-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-shortcut-manager-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-shortcut-manager-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-shortcut-manager-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-shortcut-manager-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-shortcut-manager
    COMMAND creative-suite-main-editor-shortcut-manager-tests
)
set_tests_properties(creative-suite-main-editor-shortcut-manager PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-project-tests
    project_file_test.cpp
    ../src/media/media_library.cpp
    ../src/media/media_library.h
    ../src/project/project_document.h
    ../src/project/project_file.cpp
    ../src/project/project_file.h
    ../src/timeline/timeline_transform.cpp
    ../src/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-project-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-project-tests PRIVATE
    Qt6::Core
)

target_compile_features(creative-suite-main-editor-project-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()

    add_custom_command(TARGET creative-suite-main-editor-project-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-project-tests>"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-project
    COMMAND creative-suite-main-editor-project-tests
)

add_executable(creative-suite-main-editor-media-library-tests
    media_library_test.cpp
    ../src/media/media_library.cpp
    ../src/media/media_library.h
    ../src/media/video_frame.h
    ../src/media/video_metadata.h
)

target_include_directories(creative-suite-main-editor-media-library-tests PRIVATE
    ../src
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

add_executable(creative-suite-main-editor-preview-tests
    preview_widget_test.cpp
    ../src/preview_widget.cpp
    ../src/preview_widget.h
    ../src/rendering/opengl_preview_surface.cpp
    ../src/rendering/opengl_preview_surface.h
    ../src/rendering/preview_performance_metrics.cpp
    ../src/rendering/preview_performance_metrics.h
    ../src/media/video_frame.h
)

target_include_directories(creative-suite-main-editor-preview-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-preview-tests PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

target_compile_features(creative-suite-main-editor-preview-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-preview-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-preview-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_target_property(QT_OPENGL_DLL Qt6::OpenGL IMPORTED_LOCATION_RELEASE)
    if(NOT QT_OPENGL_DLL)
        get_target_property(QT_OPENGL_DLL Qt6::OpenGL IMPORTED_LOCATION)
    endif()
    get_target_property(QT_OPENGL_WIDGETS_DLL Qt6::OpenGLWidgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_OPENGL_WIDGETS_DLL)
        get_target_property(QT_OPENGL_WIDGETS_DLL Qt6::OpenGLWidgets IMPORTED_LOCATION)
    endif()

    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-preview-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_WIDGETS_DLL}"
            "${QT_OPENGL_DLL}"
            "${QT_OPENGL_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-preview-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-preview-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-preview-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-preview
    COMMAND creative-suite-main-editor-preview-tests
)
set_tests_properties(creative-suite-main-editor-preview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1"
)

add_executable(creative-suite-main-editor-preview-performance-metrics-tests
    preview_performance_metrics_test.cpp
    ../src/rendering/preview_performance_metrics.cpp
    ../src/rendering/preview_performance_metrics.h
)

target_include_directories(creative-suite-main-editor-preview-performance-metrics-tests PRIVATE
    ../src
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

add_executable(creative-suite-main-editor-system-memory-tests
    system_memory_usage_test.cpp
    ../src/system/system_memory_usage.cpp
    ../src/system/system_memory_usage.h
)

target_include_directories(creative-suite-main-editor-system-memory-tests PRIVATE
    ../src
)

if(WIN32)
    target_link_libraries(creative-suite-main-editor-system-memory-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-system-memory-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-system-memory-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-system-memory-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-system-memory
    COMMAND creative-suite-main-editor-system-memory-tests
)

add_executable(creative-suite-main-editor-performance-usage-tests
    performance_usage_test.cpp
    ../src/system/performance_usage.cpp
    ../src/system/performance_usage.h
    ../src/system/system_memory_usage.cpp
    ../src/system/system_memory_usage.h
)

target_include_directories(creative-suite-main-editor-performance-usage-tests PRIVATE
    ../src
)

if(MSVC)
    target_compile_options(creative-suite-main-editor-performance-usage-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-performance-usage-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(creative-suite-main-editor-performance-usage-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-performance-usage-tests PRIVATE cxx_std_20)

add_test(
    NAME creative-suite-main-editor-performance-usage
    COMMAND creative-suite-main-editor-performance-usage-tests
)

add_executable(creative-suite-main-editor-system-memory-details-tests
    system_memory_details_dialog_test.cpp
    ../src/system/system_memory_usage.cpp
    ../src/system/system_memory_usage.h
    ../src/ui/system_memory_details_dialog.cpp
    ../src/ui/system_memory_details_dialog.h
    ../src/ui/system_memory_indicator.cpp
    ../src/ui/system_memory_indicator.h
)

target_include_directories(creative-suite-main-editor-system-memory-details-tests PRIVATE
    ../src
)

target_link_libraries(creative-suite-main-editor-system-memory-details-tests PRIVATE
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(creative-suite-main-editor-system-memory-details-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-system-memory-details-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-system-memory-details-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-system-memory-details-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-system-memory-details-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-system-memory-details-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-system-memory-details-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-system-memory-details-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-system-memory-details
    COMMAND creative-suite-main-editor-system-memory-details-tests
)
set_tests_properties(creative-suite-main-editor-system-memory-details PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

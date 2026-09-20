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
    ../src/media/video_playback.cpp
    ../src/media/video_playback.h
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
    ../src/playback/playback_worker.cpp
    ../src/playback/playback_worker.h
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
    ${FFMPEG_LIBRARIES}
)

target_compile_features(creative-suite-main-editor-playback-worker-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-playback-worker-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-playback-worker-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_ffmpeg_test_runtime(creative-suite-main-editor-playback-worker-tests)

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()

    add_custom_command(TARGET creative-suite-main-editor-playback-worker-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-playback-worker-tests>"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-playback-worker-errors
    COMMAND creative-suite-main-editor-playback-worker-tests
)

if(EXISTS "${REFERENCE_VIDEO}")
    add_test(
        NAME creative-suite-main-editor-playback-worker-reference
        COMMAND creative-suite-main-editor-playback-worker-tests "${REFERENCE_VIDEO}"
    )
endif()

add_executable(creative-suite-main-editor-timeline-tests
    timeline_model_test.cpp
    ../src/media/video_metadata.h
    ../src/timeline/timeline_history.cpp
    ../src/timeline/timeline_history.h
    ../src/timeline/timeline_model.cpp
    ../src/timeline/timeline_model.h
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

add_executable(creative-suite-main-editor-project-tests
    project_file_test.cpp
    ../src/project/project_document.h
    ../src/project/project_file.cpp
    ../src/project/project_file.h
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

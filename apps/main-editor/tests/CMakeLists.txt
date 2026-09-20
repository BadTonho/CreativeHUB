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
        add_custom_command(TARGET creative-suite-main-editor-video-decoder-tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${FFMPEG_TEST_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:creative-suite-main-editor-video-decoder-tests>"
            VERBATIM
        )
    endif()
endif()

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

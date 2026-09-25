add_executable(creative-suite-main-editor-logger-tests
    logger_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
)

target_include_directories(creative-suite-main-editor-logger-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
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

preserve_video_editor_test_working_directory()

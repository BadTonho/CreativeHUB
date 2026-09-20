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

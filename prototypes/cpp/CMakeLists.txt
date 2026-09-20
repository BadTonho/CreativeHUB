cmake_minimum_required(VERSION 3.24)

project(creative_suite_cpp_prototype LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(FFMPEG REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE)

add_executable(creative-suite-cpp-prototype
    src/main.cpp
)

target_compile_definitions(creative-suite-cpp-prototype PRIVATE
    PROTOTYPE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

add_executable(creative-suite-test-video-generator
    src/generate_test_video.cpp
)

find_program(GLSLC_EXECUTABLE glslc REQUIRED)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BINARY_DIR}")

add_custom_command(
    OUTPUT "${SHADER_BINARY_DIR}/video.vert.spv" "${SHADER_BINARY_DIR}/video.frag.spv"
    COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SOURCE_DIR}/video.vert" -o "${SHADER_BINARY_DIR}/video.vert.spv"
    COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SOURCE_DIR}/video.frag" -o "${SHADER_BINARY_DIR}/video.frag.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/video.vert" "${SHADER_SOURCE_DIR}/video.frag"
    VERBATIM
)

add_custom_target(creative-suite-shaders ALL
    DEPENDS "${SHADER_BINARY_DIR}/video.vert.spv" "${SHADER_BINARY_DIR}/video.frag.spv"
)

add_dependencies(creative-suite-cpp-prototype creative-suite-shaders)

function(configure_ffmpeg_target target)
    target_include_directories(${target} PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(${target} PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(${target} PRIVATE ${FFMPEG_LIBRARIES})
endfunction()

target_link_libraries(creative-suite-cpp-prototype PRIVATE
    SDL3::SDL3
)
configure_ffmpeg_target(creative-suite-cpp-prototype)
configure_ffmpeg_target(creative-suite-test-video-generator)

if (WIN32)
    target_link_libraries(creative-suite-cpp-prototype PRIVATE Psapi)
endif()

if (MSVC)
    target_compile_options(creative-suite-cpp-prototype PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-cpp-prototype PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-test-video-generator PRIVATE -Wall -Wextra -Wpedantic)
endif()

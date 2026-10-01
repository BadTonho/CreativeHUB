cmake_minimum_required(VERSION 3.24)

project(creative_suite LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

include(CTest)
include("${PROJECT_SOURCE_DIR}/cmake/AddIsolatedTest.cmake")

if(BUILD_TESTING AND MSVC)
    add_executable(creative-suite-msvc-runtime-diagnostics-probe
        cmake/test_support/msvc_runtime_diagnostics_probe.cpp
        cmake/test_support/msvc_runtime_diagnostics.cpp
    )
    target_compile_features(creative-suite-msvc-runtime-diagnostics-probe PRIVATE cxx_std_20)
    add_test(NAME creative-suite-msvc-runtime-diagnostics
        CONFIGURATIONS Debug
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE_EXECUTABLE=$<TARGET_FILE:creative-suite-msvc-runtime-diagnostics-probe>"
            -P "${PROJECT_SOURCE_DIR}/cmake/test_support/verify_msvc_runtime_diagnostics.cmake")
    set_tests_properties(creative-suite-msvc-runtime-diagnostics PROPERTIES
        TIMEOUT 30
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_VIDEO_EDITOR "Build the Video Editor application" ON)
option(BUILD_IMAGE_EDITOR "Build the standalone Image Editor application" ON)
option(BUILD_MOTION_EDITOR "Build the standalone Motion Studio application" ON)

add_subdirectory(libs)

if(BUILD_VIDEO_EDITOR)
    add_subdirectory(apps/video-editor)
endif()

if(BUILD_IMAGE_EDITOR)
    add_subdirectory(apps/image-editor)
endif()

if(BUILD_MOTION_EDITOR)
    add_subdirectory(apps/motion-editor)
endif()

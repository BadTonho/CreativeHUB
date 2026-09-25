cmake_minimum_required(VERSION 3.24)

project(creative_suite LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_VIDEO_EDITOR "Build the Video Editor application" ON)
option(BUILD_IMAGE_EDITOR "Build the standalone Image Editor application" ON)

if(BUILD_VIDEO_EDITOR)
    add_subdirectory(apps/video-editor)
endif()

if(BUILD_IMAGE_EDITOR)
    add_subdirectory(apps/image-editor)
endif()

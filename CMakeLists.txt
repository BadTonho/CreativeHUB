cmake_minimum_required(VERSION 3.24)

project(creative_suite LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_MAIN_EDITOR "Build the Main Editor application" ON)

if(BUILD_MAIN_EDITOR)
    add_subdirectory(apps/main-editor)
endif()

add_library(creative-suite-media-frame INTERFACE)
add_library(creative-suite::media-frame ALIAS creative-suite-media-frame)
target_include_directories(creative-suite-media-frame INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/media/include
)
target_compile_features(creative-suite-media-frame INTERFACE cxx_std_20)

set(_build_shared_media_default OFF)
if(BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR)
    set(_build_shared_media_default ON)
endif()
option(BUILD_VIDEO_MEDIA_LIBRARY
    "Build the shared video playback and media asset libraries"
    ${_build_shared_media_default}
)
if((BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR) AND NOT BUILD_VIDEO_MEDIA_LIBRARY)
    message(FATAL_ERROR
        "The Video Editor and Motion Studio require BUILD_VIDEO_MEDIA_LIBRARY to remain enabled.")
endif()

add_library(creative-suite-animation STATIC
    animation/src/animation.cpp
    animation/include/creative_suite/animation/animation.h
)
add_library(creative-suite::animation ALIAS creative-suite-animation)
target_include_directories(creative-suite-animation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/animation/include
)
target_link_libraries(creative-suite-animation PUBLIC creative-suite::media-frame)
target_compile_features(creative-suite-animation PUBLIC cxx_std_20)

add_library(creative-suite-composition STATIC
    composition/src/frame_compositor.cpp
    composition/include/creative_suite/composition/frame_compositor.h
)
add_library(creative-suite::composition ALIAS creative-suite-composition)
target_include_directories(creative-suite-composition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/composition/include
)
target_link_libraries(creative-suite-composition PUBLIC
    creative-suite::animation
    creative-suite::media-frame
)
target_compile_features(creative-suite-composition PUBLIC cxx_std_20)

if(BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui OpenGL)
    add_library(creative-suite-composition-opengl STATIC
        composition/src/opengl_frame_compositor.cpp
        composition/include/creative_suite/composition/opengl_frame_compositor.h)
    add_library(creative-suite::composition-opengl ALIAS creative-suite-composition-opengl)
    target_link_libraries(creative-suite-composition-opengl PUBLIC
        creative-suite::composition Qt6::Gui Qt6::OpenGL)
    target_compile_features(creative-suite-composition-opengl PUBLIC cxx_std_20)
endif()

add_library(creative-suite-diagnostics STATIC
    diagnostics/src/logger.cpp
    diagnostics/include/creative_suite/diagnostics/logger.h
)
add_library(creative-suite::diagnostics ALIAS creative-suite-diagnostics)
target_include_directories(creative-suite-diagnostics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/include
)
target_compile_features(creative-suite-diagnostics PUBLIC cxx_std_20)

add_library(creative-suite-system-monitor STATIC
    system-monitor/src/performance_usage.cpp
    system-monitor/src/system_memory_usage.cpp
    system-monitor/include/creative_suite/system_monitor/performance_usage.h
    system-monitor/include/creative_suite/system_monitor/system_memory_usage.h
)
add_library(creative-suite::system-monitor ALIAS creative-suite-system-monitor)
target_include_directories(creative-suite-system-monitor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/system-monitor/include
)
target_compile_features(creative-suite-system-monitor PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(creative-suite-system-monitor PUBLIC Psapi)
endif()

if(BUILD_VIDEO_MEDIA_LIBRARY)
    add_subdirectory(media)
endif()

if(BUILD_VIDEO_EDITOR OR BUILD_IMAGE_EDITOR OR BUILD_MOTION_EDITOR)
    add_subdirectory(shortcuts)
endif()

foreach(shared_target IN ITEMS
        creative-suite-animation
        creative-suite-composition
        creative-suite-composition-opengl
        creative-suite-diagnostics
        creative-suite-system-monitor)
    if(NOT TARGET ${shared_target})
        continue()
    endif()
    if(MSVC)
        target_compile_options(${shared_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${shared_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

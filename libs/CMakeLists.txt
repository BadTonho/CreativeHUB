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

add_library(creative-suite-effects STATIC
    effects/src/effects.cpp
    effects/src/gaussian_blur.cpp
    effects/include/creative_suite/effects/gaussian_blur.h
    effects/include/creative_suite/effects/effects.h
)
add_library(creative-suite::effects ALIAS creative-suite-effects)
target_include_directories(creative-suite-effects PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/include
)
target_link_libraries(creative-suite-effects PUBLIC creative-suite::media-frame)
target_compile_features(creative-suite-effects PUBLIC cxx_std_20)

add_library(creative-suite-composition STATIC
    composition/src/frame_compositor.cpp
    composition/include/creative_suite/composition/affine_transform.h
    composition/include/creative_suite/composition/frame_compositor.h
      composition/include/creative_suite/composition/shape_mask.h
)
add_library(creative-suite::composition ALIAS creative-suite-composition)
target_include_directories(creative-suite-composition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/composition/include
)
target_link_libraries(creative-suite-composition PUBLIC
    creative-suite::animation
    creative-suite::effects
    creative-suite::media-frame
)
target_compile_features(creative-suite-composition PUBLIC cxx_std_20)

if(BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui)
    add_library(creative-suite-composition-exchange STATIC
        composition-exchange/src/document.cpp composition-exchange/src/raster.cpp)
    add_library(creative-suite::composition-exchange ALIAS creative-suite-composition-exchange)
    target_include_directories(creative-suite-composition-exchange PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/composition-exchange/include)
    target_link_libraries(creative-suite-composition-exchange PUBLIC
        creative-suite::composition creative-suite::animation creative-suite::effects Qt6::Core Qt6::Gui)
endif()

if(BUILD_VIDEO_EDITOR OR BUILD_MOTION_EDITOR OR BUILD_IMAGE_EDITOR)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui OpenGL)
    add_library(creative-suite-composition-opengl STATIC
        composition/src/opengl_frame_compositor.cpp
        composition/src/gpu_frame_compositor.cpp
        composition/src/vulkan_frame_compositor.cpp
        composition/src/windows_nv12_converter.cpp
        composition/src/windows_video_interop.cpp
        composition/include/creative_suite/composition/opengl_frame_compositor.h)
    add_library(creative-suite::composition-opengl ALIAS creative-suite-composition-opengl)
    target_link_libraries(creative-suite-composition-opengl PUBLIC
        creative-suite::composition Qt6::Gui Qt6::OpenGL)
    if(BUILD_VIDEO_MEDIA_LIBRARY)
        target_link_libraries(creative-suite-composition-opengl PUBLIC creative-suite::video-media)
        target_compile_definitions(creative-suite-composition-opengl PRIVATE CS_NATIVE_VIDEO_INTEROP=1)
    endif()
    if(WIN32 AND BUILD_VIDEO_MEDIA_LIBRARY)
        target_link_libraries(creative-suite-composition-opengl PRIVATE d3d11 d3dcompiler opengl32)
    endif()
    target_compile_features(creative-suite-composition-opengl PUBLIC cxx_std_20)
    option(ENABLE_VULKAN_RENDERER "Build the optional Vulkan composition backend" ON)
    if(ENABLE_VULKAN_RENDERER)
        find_package(Vulkan QUIET)
        find_program(CS_GLSLANG NAMES glslangValidator glslang HINTS
            "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/glslang")
        if(Vulkan_INCLUDE_DIR AND CS_GLSLANG)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
            foreach(variant IN ITEMS composition composition_precise)
                set(defines)
                if(variant STREQUAL "composition_precise")
                    set(defines -DPRECISE_ROTATION=1)
                endif()
                set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${variant}_spirv.h")
                add_custom_command(OUTPUT "${output}"
                    COMMAND "${CS_GLSLANG}" -V --target-env vulkan1.1 ${defines}
                        --vn ${variant}_spirv -o "${output}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/composition/shaders/composition.comp"
                    DEPENDS composition/shaders/composition.comp VERBATIM)
                target_sources(creative-suite-composition-opengl PRIVATE "${output}")
            endforeach()
            foreach(variant IN ITEMS interop_import interop_export)
                set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${variant}_spirv.h")
                add_custom_command(OUTPUT "${output}"
                    COMMAND "${CS_GLSLANG}" -V --target-env vulkan1.1 --vn ${variant}_spirv -o "${output}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/composition/shaders/${variant}.comp"
                    DEPENDS composition/shaders/${variant}.comp VERBATIM)
                target_sources(creative-suite-composition-opengl PRIVATE "${output}")
            endforeach()
            target_include_directories(creative-suite-composition-opengl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders")
            target_compile_definitions(creative-suite-composition-opengl PRIVATE CS_HAS_VULKAN=1)
            target_include_directories(creative-suite-composition-opengl PRIVATE ${Vulkan_INCLUDE_DIR})
        else()
            message(STATUS "Vulkan renderer unavailable: Vulkan headers/loader or glslang tool missing; OpenGL remains available")
        endif()
    endif()

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

if(TARGET creative-suite-composition-opengl)
    target_link_libraries(creative-suite-composition-opengl PRIVATE creative-suite::diagnostics)
endif()

if(BUILD_VIDEO_EDITOR OR BUILD_IMAGE_EDITOR OR BUILD_MOTION_EDITOR OR BUILD_CREATIVE_HUB)
    add_subdirectory(translations)
endif()

if(BUILD_VIDEO_EDITOR OR BUILD_IMAGE_EDITOR OR BUILD_MOTION_EDITOR OR BUILD_CREATIVE_HUB)
    add_subdirectory(updater)
endif()

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
        creative-suite-effects
        creative-suite-composition
        creative-suite-composition-opengl
        creative-suite-diagnostics
        creative-suite-updater
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

find_package(FFMPEG MODULE REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWRESAMPLE SWSCALE)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)
set_property(GLOBAL PROPERTY CREATIVE_SUITE_FFMPEG_LIBRARY_DIRS "${FFMPEG_LIBRARY_DIRS}")

function(creative_suite_deploy_media_runtime target_name)
    if(WIN32)
        get_property(media_ffmpeg_library_dirs GLOBAL PROPERTY CREATIVE_SUITE_FFMPEG_LIBRARY_DIRS)
        set(media_ffmpeg_runtime_dlls)
        foreach(ffmpeg_library_dir IN LISTS media_ffmpeg_library_dirs)
            if(ffmpeg_library_dir MATCHES "[/\\\\]debug([/\\\\]|$)")
                continue()
            endif()
            get_filename_component(ffmpeg_root_dir "${ffmpeg_library_dir}" DIRECTORY)
            foreach(ffmpeg_runtime_name IN ITEMS avcodec avdevice avfilter avformat avutil swresample swscale)
                file(GLOB ffmpeg_runtime_matches
                    "${ffmpeg_root_dir}/bin/${ffmpeg_runtime_name}-*.dll")
                list(APPEND media_ffmpeg_runtime_dlls ${ffmpeg_runtime_matches})
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES media_ffmpeg_runtime_dlls)
        if(media_ffmpeg_runtime_dlls)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${media_ffmpeg_runtime_dlls}
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM
            )
            if(ARGC GREATER 1)
                install(FILES ${media_ffmpeg_runtime_dlls}
                    DESTINATION ${CMAKE_INSTALL_BINDIR}
                    COMPONENT "${ARGV1}"
                )
            endif()
        endif()
    endif()
endfunction()

add_library(creative-suite-video-media STATIC
    src/video_playback.cpp
    include/creative_suite/media/media_error.h
    include/creative_suite/media/video_frame.h
    include/creative_suite/media/video_playback.h
)
add_library(creative-suite::video-media ALIAS creative-suite-video-media)
target_include_directories(creative-suite-video-media PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(creative-suite-video-media PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(creative-suite-video-media PUBLIC
    creative-suite::media-frame
    creative-suite::diagnostics
    ${FFMPEG_LIBRARIES}
)
target_compile_features(creative-suite-video-media PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-video-media PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-video-media PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(creative-suite-video-encoding STATIC
    src/video_encoder.cpp
    include/creative_suite/media/video_encoder.h
)
add_library(creative-suite::video-encoding ALIAS creative-suite-video-encoding)
target_include_directories(creative-suite-video-encoding PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(creative-suite-video-encoding PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(creative-suite-video-encoding PUBLIC
    creative-suite::media-frame
    ${FFMPEG_LIBRARIES}
)
target_compile_features(creative-suite-video-encoding PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-video-encoding PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-video-encoding PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(creative-suite-media-assets STATIC
    src/media_importer.cpp
    src/media_library.cpp
    src/still_image_decoder.cpp
    src/video_decoder.cpp
    src/video_probe.cpp
    include/creative_suite/media/media_importer.h
    include/creative_suite/media/media_library.h
    include/creative_suite/media/still_image_decoder.h
    include/creative_suite/media/video_decoder.h
    include/creative_suite/media/video_metadata.h
    include/creative_suite/media/video_probe.h
)
add_library(creative-suite::media-assets ALIAS creative-suite-media-assets)
target_include_directories(creative-suite-media-assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(creative-suite-media-assets PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(creative-suite-media-assets PUBLIC
    creative-suite::media-frame
    creative-suite::diagnostics
    creative-suite::video-media
    Qt6::Gui
    ${FFMPEG_LIBRARIES}
)
target_compile_features(creative-suite-media-assets PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-media-assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-media-assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

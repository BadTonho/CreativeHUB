find_package(FFMPEG MODULE REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE)

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

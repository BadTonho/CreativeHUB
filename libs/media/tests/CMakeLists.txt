add_executable(creative-suite-media-assets-tests
    media_assets_test.cpp
)
target_link_libraries(creative-suite-media-assets-tests PRIVATE
    creative-suite::media-assets
)
target_compile_features(creative-suite-media-assets-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-media-assets-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-media-assets-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-media-assets
    COMMAND creative-suite-media-assets-tests)

add_executable(creative-suite-video-encoder-tests
    video_encoder_test.cpp
)
target_link_libraries(creative-suite-video-encoder-tests PRIVATE
    creative-suite::video-encoding
    creative-suite::video-media
)
target_compile_features(creative-suite-video-encoder-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-video-encoder-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-video-encoder-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
creative_suite_deploy_media_runtime(creative-suite-video-encoder-tests)
add_creative_suite_test(
    NAME creative-suite-video-encoder
    COMMAND creative-suite-video-encoder-tests)

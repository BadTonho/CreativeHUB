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

add_test(NAME creative-suite-media-assets COMMAND creative-suite-media-assets-tests)

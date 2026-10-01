add_executable(creative-suite-media-assets-tests
    media_assets_test.cpp
)
target_link_libraries(creative-suite-media-assets-tests PRIVATE
    creative-suite::media-assets
)
target_compile_features(creative-suite-media-assets-tests PRIVATE cxx_std_20)
if(WIN32)
    get_target_property(MEDIA_TEST_QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT MEDIA_TEST_QT_CORE_DLL)
        get_target_property(MEDIA_TEST_QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(MEDIA_TEST_QT_BIN_DIR "${MEDIA_TEST_QT_CORE_DLL}" DIRECTORY)
    find_program(MEDIA_TEST_QT_DEPLOY_TOOL windeployqt HINTS "${MEDIA_TEST_QT_BIN_DIR}" REQUIRED)
    add_custom_command(TARGET creative-suite-media-assets-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_CONFIGURATION=$<CONFIG>"
            "-DQT_DEPLOY_TOOL=${MEDIA_TEST_QT_DEPLOY_TOOL}"
            "-DQT_TARGET_FILE=$<TARGET_FILE:creative-suite-media-assets-tests>"
            -P "${PROJECT_SOURCE_DIR}/cmake/DeployQtMediaTestRuntime.cmake"
        VERBATIM
    )
endif()
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

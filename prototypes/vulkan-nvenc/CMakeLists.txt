find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)
if(NOT TARGET creative-suite::video-encoding OR NOT TARGET creative-suite::composition-opengl)
    message(FATAL_ERROR "Vulkan/NVENC prototype requires the shared video and composition libraries")
endif()
add_executable(creative-suite-vulkan-nvenc-prototype main.cpp)
target_link_libraries(creative-suite-vulkan-nvenc-prototype PRIVATE
    creative-suite::composition-opengl creative-suite::video-encoding creative-suite::diagnostics Qt6::Gui)
target_include_directories(creative-suite-vulkan-nvenc-prototype PRIVATE ${PROJECT_SOURCE_DIR}/libs/media/tests)
target_compile_features(creative-suite-vulkan-nvenc-prototype PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-vulkan-nvenc-prototype)
if(COMMAND deploy_qt_test_runtime)
    deploy_qt_test_runtime(creative-suite-vulkan-nvenc-prototype)
elseif(WIN32)
    get_target_property(qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(qt_bin "${qt_core_dll}" DIRECTORY)
    find_program(qt_deploy windeployqt HINTS "${qt_bin}" REQUIRED)
    add_custom_command(TARGET creative-suite-vulkan-nvenc-prototype POST_BUILD
        COMMAND "${qt_deploy}" --no-translations --no-compiler-runtime "$<TARGET_FILE:creative-suite-vulkan-nvenc-prototype>" VERBATIM)
endif()
if(BUILD_TESTING)
    add_creative_suite_test(NAME creative-suite-vulkan-nvenc-prototype SKIP_RETURN_CODE 77
        COMMAND creative-suite-vulkan-nvenc-prototype --self-test)
    set_tests_properties(creative-suite-vulkan-nvenc-prototype PROPERTIES TIMEOUT 120)
    add_creative_suite_test(NAME creative-suite-vulkan-decoded-pipeline SKIP_RETURN_CODE 77
        COMMAND creative-suite-vulkan-nvenc-prototype --decode-self-test)
    set_tests_properties(creative-suite-vulkan-decoded-pipeline PROPERTIES TIMEOUT 180)
endif()


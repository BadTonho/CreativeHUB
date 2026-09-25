add_executable(creative-suite-main-editor-effects-panel-tests
    effects_panel_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_catalog.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_catalog.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_favorites_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_favorites_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_toolbox_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_toolbox_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_list_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/effects/effects_list_widget.h
)

target_include_directories(creative-suite-main-editor-effects-panel-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-effects-panel-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-effects-panel-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-effects-panel-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-effects-panel-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

deploy_qt_test_runtime(creative-suite-main-editor-effects-panel-tests)

add_test(
    NAME creative-suite-main-editor-effects-panel
    COMMAND creative-suite-main-editor-effects-panel-tests
)
set_tests_properties(creative-suite-main-editor-effects-panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

preserve_video_editor_test_working_directory()

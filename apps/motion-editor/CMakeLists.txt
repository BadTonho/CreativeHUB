include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-motion-editor-core STATIC
    src/model/composition_document.cpp
    src/model/composition_document.h
    src/model/motion_project_data.h
    src/persistence/motion_recovery_store.cpp
    src/persistence/motion_recovery_store.h
    src/persistence/motion_document_store.cpp
    src/persistence/motion_document_store.h
    src/settings/autosave_preferences.cpp
    src/settings/autosave_preferences.h
    src/ui/autosave_recovery_dialog.cpp
    src/ui/autosave_recovery_dialog.h
    src/ui/composition_viewer.cpp
    src/ui/composition_viewer.h
    src/ui/composition_history.cpp
    src/ui/composition_history.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/media_pool_widget.cpp
    src/ui/media_pool_widget.h
    src/ui/new_composition_dialog.cpp
    src/ui/new_composition_dialog.h
    src/ui/shortcut_settings_dialog.cpp
    src/ui/shortcut_settings_dialog.h
    src/ui/preview_renderer.cpp
    src/ui/preview_renderer.h
    src/ui/property_curve_editor.cpp
    src/ui/property_curve_editor.h
    src/ui/composition_frame_renderer.cpp
    src/ui/composition_frame_renderer.h
    src/ui/motion_video_export.cpp
    src/ui/motion_video_export.h
    src/ui/motion_video_export_dialog.cpp
    src/ui/motion_video_export_dialog.h
    src/ui/layer_content_renderer.cpp
    src/ui/layer_content_renderer.h
    src/ui/layer_effect_processor.cpp
    src/ui/layer_effect_processor.h
    src/ui/timeline_navigator.cpp
    src/ui/timeline_navigator.h
    src/ui/timeline_navigator_math.h
)
target_include_directories(creative-suite-motion-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(creative-suite-motion-editor-core PUBLIC
    creative-suite::animation
    creative-suite::composition
    creative-suite::diagnostics
    creative-suite::media-assets
    creative-suite::shortcuts
    creative-suite::video-media
    creative-suite::video-encoding
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-core PUBLIC cxx_std_20)

add_executable(creative-suite-motion-editor
    src/main.cpp
)
target_link_libraries(creative-suite-motion-editor PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-motion-editor)

if(MSVC)
    target_compile_options(creative-suite-motion-editor-core PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-motion-editor-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(creative-suite-motion-editor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

install(TARGETS creative-suite-motion-editor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET creative-suite-motion-editor
    OUTPUT_SCRIPT motion_editor_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${motion_editor_deploy_script})

if(WIN32)
    get_target_property(QT_MOTION_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_MOTION_CORE_DLL)
        get_target_property(QT_MOTION_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_MOTION_BIN_DIR "${QT_MOTION_CORE_DLL}" DIRECTORY)
    find_program(QT_MOTION_DEPLOY_TOOL windeployqt HINTS "${QT_MOTION_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-motion-editor POST_BUILD
        COMMAND "${QT_MOTION_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-motion-editor>"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

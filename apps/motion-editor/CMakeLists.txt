include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets LinguistTools)

add_library(creative-suite-motion-editor-core STATIC
    src/audio/audio_envelope_analyzer.cpp
    src/audio/audio_envelope_analyzer.h
    src/diagnostics/performance_metrics.cpp
    src/diagnostics/performance_metrics.h
    src/export/motion_video_export.cpp
    src/export/motion_video_export.h
    src/model/composition_interchange.cpp
    src/model/composition_document.cpp
    src/model/composition_document.h
    src/model/layer_transform_hierarchy.cpp
    src/model/layer_anchor.cpp
    src/model/layer_scale.cpp
    src/model/layer_transform_hierarchy.h
    src/model/motion_project_data.h
    src/persistence/motion_recovery_store.cpp
    src/persistence/motion_recovery_store.h
    src/persistence/motion_document_store.cpp
    src/persistence/motion_document_store.h
    src/rendering/preview_renderer.cpp
    src/rendering/preview_renderer.h
    src/rendering/composition_frame_renderer.cpp
    src/rendering/composition_frame_renderer.h
    src/rendering/layer_content_renderer.cpp
    src/rendering/layer_content_renderer.h
    src/rendering/layer_effect_processor.cpp
    src/rendering/layer_effect_processor.h
    src/rendering/masked_layer_renderer.cpp
    src/rendering/masked_layer_renderer.h
    src/rendering/layer_effect_worker_pool.cpp
    src/rendering/layer_effect_worker_pool.h
    src/settings/autosave_preferences.cpp
    src/settings/autosave_preferences.h
    src/settings/gpu_composition_preferences.cpp
    src/settings/gpu_composition_preferences.h
    src/ui/workers/audio_keyframe_generation.cpp
    src/ui/workers/audio_keyframe_generation.h
    src/ui/viewer/composition_viewer.cpp
    src/ui/viewer/composition_viewer.h
    src/application/history/composition_history.cpp
    src/application/history/composition_history.h
    src/ui/main_window/main_window_interchange.cpp
    src/ui/main_window/main_window.cpp
    src/ui/main_window/main_window.h
    src/ui/main_window/main_window_documents.cpp
    src/ui/main_window/main_window_jobs.cpp
    src/ui/main_window/main_window_settings_recovery.cpp
    src/ui/main_window/main_window_workspace.cpp
    src/ui/main_window/main_window_editing.cpp
    src/ui/main_window/main_window_anchor.cpp
    src/ui/main_window/main_window_scale.cpp
    src/ui/main_window/main_window_preview.cpp
    src/ui/main_window/main_window_support.cpp
    src/ui/main_window/main_window_support.h
    src/ui/main_window/main_window_image_links.cpp
    src/ui/dialogs/autosave_recovery_dialog.cpp
    src/ui/dialogs/autosave_recovery_dialog.h
    src/ui/dialogs/audio_keyframe_dialog.cpp
    src/ui/dialogs/audio_keyframe_dialog.h
    src/ui/dialogs/general_settings_dialog.cpp
    src/ui/dialogs/general_settings_dialog.h
    src/ui/dialogs/motion_video_export_dialog.cpp
    src/ui/dialogs/motion_video_export_dialog.h
    src/ui/dialogs/new_composition_dialog.cpp
    src/ui/dialogs/new_composition_dialog.h
    src/ui/dialogs/shortcut_settings_dialog.cpp
    src/ui/dialogs/shortcut_settings_dialog.h
    src/ui/media_pool/media_pool_widget.cpp
    src/ui/media_pool/media_pool_widget.h
    src/ui/inspector/mask_inspector_widget.cpp
    src/ui/inspector/mask_inspector_widget.h
    src/ui/inspector/inspector_widget.cpp
    src/ui/inspector/inspector_widget.h
    src/ui/timeline/graph_editor/property_curve_editor.cpp
    src/ui/timeline/graph_editor/property_curve_editor.h
    src/ui/timeline/timeline_controls.cpp
    src/ui/timeline/timeline_controls.h
    src/ui/timeline/timeline_layer_tracks.cpp
    src/ui/timeline/timeline_layer_tracks.h
    src/ui/timeline/timeline_navigator.cpp
    src/ui/timeline/timeline_navigator.h
    src/ui/timeline/timeline_navigator_math.h
    src/ui/timeline/timeline_ruler.cpp
    src/ui/timeline/timeline_ruler.h
    src/ui/timeline/timeline_types.h
    src/ui/timeline/timeline_view_mapping.h
    src/ui/workspace/motion_workspace.cpp
    src/ui/workspace/motion_workspace.h
)
target_include_directories(creative-suite-motion-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/motion-handoff/include
)
target_link_libraries(creative-suite-motion-editor-core PUBLIC
    creative-suite::composition-exchange
    creative-suite::translations
    creative-suite::animation
    creative-suite::composition
    creative-suite::composition-opengl
    creative-suite::effects
    creative-suite::diagnostics
    creative-suite::system-monitor
    creative-suite::media-assets
    creative-suite::shortcuts
    creative-suite::video-media
    creative-suite::video-encoding
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)
target_compile_features(creative-suite-motion-editor-core PUBLIC cxx_std_20)

add_executable(creative-suite-motion-editor
    src/main.cpp
    resources/app_icon.qrc
    resources/translations.qrc
)
target_link_libraries(creative-suite-motion-editor PRIVATE
    creative-suite-motion-editor-core
    creative-suite::translations
    creative-suite::updater
    Qt6::Widgets
)
target_compile_definitions(creative-suite-motion-editor PRIVATE
    CREATIVE_SUITE_APP_VERSION="${CREATIVE_SUITE_VERSION_MOTION_EDITOR}")
target_compile_features(creative-suite-motion-editor PRIVATE cxx_std_20)

file(GLOB_RECURSE MOTION_EDITOR_TRANSLATION_INPUTS CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
)
list(APPEND MOTION_EDITOR_TRANSLATION_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

set(MOTION_EDITOR_TRANSLATION_TS "${CMAKE_SOURCE_DIR}/languages/motion-editor/pt_BR.ts")
set(MOTION_EDITOR_TRANSLATION_QM "${CMAKE_SOURCE_DIR}/languages/motion-editor/pt_BR.qm")
set(MOTION_EDITOR_GENERATED_TRANSLATION_QM
    "${CMAKE_CURRENT_BINARY_DIR}/translations/motion-editor/pt_BR.qm")
add_custom_command(
    OUTPUT "${MOTION_EDITOR_GENERATED_TRANSLATION_QM}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/translations/motion-editor"
    COMMAND "$<TARGET_FILE:Qt6::lrelease>" "${MOTION_EDITOR_TRANSLATION_TS}"
            -qm "${MOTION_EDITOR_GENERATED_TRANSLATION_QM}"
    DEPENDS "${MOTION_EDITOR_TRANSLATION_TS}"
    VERBATIM
)
add_custom_target(creative-suite-motion-editor-translation-catalog
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${MOTION_EDITOR_GENERATED_TRANSLATION_QM}"
            "${MOTION_EDITOR_TRANSLATION_QM}"
    DEPENDS "${MOTION_EDITOR_GENERATED_TRANSLATION_QM}"
            "${MOTION_EDITOR_TRANSLATION_QM}"
    COMMENT "Checking that the tracked Motion Studio catalog matches its TS source"
    VERBATIM
)
add_dependencies(creative-suite-motion-editor creative-suite-motion-editor-translation-catalog)

add_custom_target(update-motion-editor-translations
    COMMAND "$<TARGET_FILE:Qt6::lupdate>" -no-obsolete
            -source-language en_US -target-language pt_BR
            ${MOTION_EDITOR_TRANSLATION_INPUTS}
            -ts "${MOTION_EDITOR_TRANSLATION_TS}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Updating the Motion Studio translation source"
    VERBATIM
)

creative_suite_deploy_media_runtime(creative-suite-motion-editor motion-editor)

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

if(WIN32)
    set(APP_ICON_FILE "${CMAKE_SOURCE_DIR}/docs/assets/app-icons/motion-studio.ico")
    set(APP_PRODUCT_NAME "Motion Studio")
    set(APP_INTERNAL_ID "motion-editor")
    set(APP_EXECUTABLE_NAME "creative-suite-motion-editor.exe")
    set(APP_VERSION_STRING "${CREATIVE_SUITE_VERSION_MOTION_EDITOR}")
    string(REPLACE "." "," APP_FILE_VERSION "${APP_VERSION_STRING}.0")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/windows_app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/motion_studio_app_icon.rc"
        @ONLY
    )
    target_sources(creative-suite-motion-editor PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/motion_studio_app_icon.rc"
    )
endif()

install(TARGETS creative-suite-motion-editor COMPONENT motion-editor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET creative-suite-motion-editor
    OUTPUT_SCRIPT motion_editor_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${motion_editor_deploy_script} COMPONENT motion-editor)

if(WIN32)
    get_target_property(QT_MOTION_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_MOTION_CORE_DLL)
        get_target_property(QT_MOTION_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_MOTION_BIN_DIR "${QT_MOTION_CORE_DLL}" DIRECTORY)
    find_program(QT_MOTION_DEPLOY_TOOL windeployqt HINTS "${QT_MOTION_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-motion-editor POST_BUILD
        COMMAND "${QT_MOTION_DEPLOY_TOOL}"
            --qtpaths "${QT_MOTION_BIN_DIR}/qtpaths.exe"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-motion-editor>"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

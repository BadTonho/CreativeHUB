include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-image-editor-core STATIC
    src/core/document/image_exporter.h
    src/core/document/image_exporter.cpp
    src/core/document/image_document_codec.cpp
    src/core/document/image_document_codec.h
    src/core/document/image_document_geometry.cpp
    src/core/document/image_document_geometry.h
    src/core/document/image_document_history.cpp
    src/core/document/image_document_history.h
    src/core/document/image_document_renderer.cpp
    src/core/document/image_document_renderer.h
    src/core/document/image_document_utils.h
    src/core/document/image_layer_mask_editor.cpp
    src/core/document/image_layer_mask_editor.h
    src/core/document/image_layer_stack_editor.cpp
    src/core/document/image_layer_stack_editor.h
    src/core/document/image_raster_import.cpp
    src/core/document/image_raster_import.h
    src/core/document/image_document_store.cpp
    src/core/document/image_document_store.h
    src/core/document/image_document_session.cpp
    src/core/document/image_document_session.h
    src/core/diagnostics/image_editor_logger.cpp
    src/core/diagnostics/image_editor_logger.h
    src/core/recovery/recovery_store.cpp
    src/core/recovery/recovery_store.h
)

target_include_directories(creative-suite-image-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/document
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/recovery
)

target_link_libraries(creative-suite-image-editor-core PUBLIC
    Qt6::Core
    Qt6::Gui
)

target_compile_features(creative-suite-image-editor-core PUBLIC cxx_std_20)

add_executable(creative-suite-image-editor
    src/app/main.cpp
    resources/app_icon.qrc
    src/ui/windows/image_editor_window.cpp
    src/ui/windows/image_editor_window.h
    src/ui/export/image_export_controller.cpp
    src/ui/export/image_export_controller.h
    src/ui/export/image_export_worker.cpp
    src/ui/export/image_export_worker.h
    src/ui/import/image_import_controller.cpp
    src/ui/import/image_import_controller.h
    src/ui/dialogs/image_export_dialog.cpp
    src/ui/dialogs/image_export_dialog.h
    src/ui/dialogs/new_canvas_dialog.cpp
    src/ui/dialogs/new_canvas_dialog.h
    src/ui/dialogs/canvas_size_dialog.cpp
    src/ui/dialogs/canvas_size_dialog.h
    src/ui/dialogs/shortcut_settings_dialog.cpp
    src/ui/dialogs/shortcut_settings_dialog.h
    src/ui/canvas/image_canvas.cpp
    src/ui/canvas/image_canvas.h
    src/ui/tools/brush/brush_tool.cpp
    src/ui/tools/brush/brush_tool.h
    src/ui/tools/brush/paint_tool.cpp
    src/ui/tools/brush/paint_tool.h
    src/ui/tools/brush/eraser_tool.cpp
    src/ui/tools/brush/eraser_tool.h
    src/ui/tools/crop/crop_tool.cpp
    src/ui/tools/crop/crop_tool.h
    src/ui/tools/selection/area_selection_tool.cpp
    src/ui/tools/selection/area_selection_tool.h
    src/ui/tools/selection/object/object_selection_tool.cpp
    src/ui/tools/selection/object/object_selection_tool.h
    src/ui/tools/shapes/shape_tool.cpp
    src/ui/tools/shapes/shape_tool.h
    src/ui/tools/text/text_tool.cpp
    src/ui/tools/text/text_tool.h
    src/ui/tools/tool_sidebar.cpp
    src/ui/tools/tool_sidebar.h
    src/ui/layers/layer_panel.cpp
    src/ui/layers/layer_panel.h
)

target_include_directories(creative-suite-image-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/canvas
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/export
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/import
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/layers
)

target_link_libraries(creative-suite-image-editor PRIVATE
    creative-suite-image-editor-core
    creative-suite::shortcuts
    Qt6::Widgets
)

target_compile_features(creative-suite-image-editor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-image-editor-core PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-image-editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-image-editor-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-image-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(creative-suite-image-editor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(WIN32)
    set(APP_ICON_FILE "${CMAKE_SOURCE_DIR}/docs/assets/app-icons/image-editor.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/windows_app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/image_editor_app_icon.rc"
        @ONLY
    )
    target_sources(creative-suite-image-editor PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/image_editor_app_icon.rc"
    )
endif()

install(TARGETS creative-suite-image-editor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET creative-suite-image-editor
    OUTPUT_SCRIPT image_editor_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${image_editor_deploy_script})

if(WIN32)
    set(CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR "" CACHE PATH
        "Directory containing Qt image format plugins qwebp and qtiff for the active Qt build")

    get_target_property(QT_IMAGE_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_IMAGE_CORE_DLL)
        get_target_property(QT_IMAGE_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_IMAGE_BIN_DIR "${QT_IMAGE_CORE_DLL}" DIRECTORY)
    find_program(QT_IMAGE_DEPLOY_TOOL windeployqt HINTS "${QT_IMAGE_BIN_DIR}" REQUIRED)

    if(NOT CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR)
        set(_qt_imageformats_candidates
            "${QT_IMAGE_BIN_DIR}/../plugins/imageformats")
        if(VCPKG_INSTALLED_DIR)
            list(APPEND _qt_imageformats_candidates
                "${VCPKG_INSTALLED_DIR}/plugins/imageformats")
        endif()
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            list(APPEND _qt_imageformats_candidates
                "${_prefix}/plugins/imageformats")
        endforeach()
        foreach(_candidate IN LISTS _qt_imageformats_candidates)
            if(EXISTS "${_candidate}/qwebp.dll"
                    AND EXISTS "${_candidate}/qtiff.dll"
                    AND EXISTS "${_candidate}/qwebpd.dll"
                    AND EXISTS "${_candidate}/qtiffd.dll")
                set(CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR "${_candidate}")
                break()
            endif()
        endforeach()
    endif()

    foreach(_plugin IN ITEMS qwebp.dll qtiff.dll qwebpd.dll qtiffd.dll)
        if(NOT EXISTS "${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/${_plugin}")
            message(FATAL_ERROR
                "Required Qt image format plugin '${_plugin}' was not found in "
                "'${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}'. Install the "
                "Qt Image Formats add-on matching the active Qt version and "
                "MSVC runtime, or set CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR.")
        endif()
    endforeach()

    function(creative_suite_deploy_qt_imageformat_plugins target_name)
        add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/imageformats"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qwebpd.dll,${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qwebp.dll>"
                "$<IF:$<CONFIG:Debug>,${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qtiffd.dll,${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qtiff.dll>"
                "$<TARGET_FILE_DIR:${target_name}>/imageformats"
            VERBATIM
        )
    endfunction()

    add_custom_command(TARGET creative-suite-image-editor POST_BUILD
        COMMAND "${QT_IMAGE_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            --add-plugin-types imageformats
            "$<TARGET_FILE:creative-suite-image-editor>"
        VERBATIM
    )
    creative_suite_deploy_qt_imageformat_plugins(creative-suite-image-editor)

    install(FILES
        "${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qwebp.dll"
        "${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qtiff.dll"
        DESTINATION plugins/imageformats
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
    )
    install(FILES
        "${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qwebpd.dll"
        "${CREATIVE_SUITE_QT_IMAGEFORMATS_PLUGIN_DIR}/qtiffd.dll"
        DESTINATION plugins/imageformats
        CONFIGURATIONS Debug
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

add_executable(creative-suite-image-editor-core-tests
    image_editor_core_test.cpp
)

target_link_libraries(creative-suite-image-editor-core-tests PRIVATE
    creative-suite-image-editor-core
    Qt6::Test
)
target_compile_features(creative-suite-image-editor-core-tests PRIVATE cxx_std_20)

add_creative_suite_test(
    NAME creative-suite-image-editor-core
    COMMAND creative-suite-image-editor-core-tests)
set_tests_properties(creative-suite-image-editor-core PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-image-editor-mask-tests image_editor_mask_test.cpp)
add_executable(creative-suite-image-editor-raster-tests image_editor_raster_test.cpp)
target_link_libraries(creative-suite-image-editor-raster-tests PRIVATE creative-suite-image-editor-core Qt6::Test)
target_compile_features(creative-suite-image-editor-raster-tests PRIVATE cxx_std_20)
add_creative_suite_test(NAME creative-suite-image-editor-raster COMMAND creative-suite-image-editor-raster-tests)
set_tests_properties(creative-suite-image-editor-raster PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_link_libraries(creative-suite-image-editor-mask-tests PRIVATE
    creative-suite-image-editor-core Qt6::Test)
target_compile_features(creative-suite-image-editor-mask-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-image-editor-masks
    COMMAND creative-suite-image-editor-mask-tests)
set_tests_properties(creative-suite-image-editor-masks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(creative-suite-image-editor-format-tests
    image_editor_format_test.cpp
)
target_link_libraries(creative-suite-image-editor-format-tests PRIVATE
    creative-suite-image-editor-core
    Qt6::Core
    Qt6::Gui
)
target_compile_features(creative-suite-image-editor-format-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-image-editor-image-formats
    COMMAND creative-suite-image-editor-format-tests)

if(WIN32)
    add_dependencies(
        creative-suite-image-editor-format-tests
        creative-suite-image-editor)
    add_creative_suite_test(
        NAME creative-suite-image-editor-deployed-image-formats
        COMMAND creative-suite-image-editor-format-tests
            --plugin-root "$<TARGET_FILE_DIR:creative-suite-image-editor>")
endif()

add_executable(creative-suite-image-editor-ui-tests
    image_editor_ui_test.cpp
    image_editor_mask_ui_test.cpp
    image_editor_raster_ui_test.cpp
    image_editor_deletion_ui_test.cpp
    ../src/ui/windows/image_editor_window.cpp
    ../src/ui/windows/image_editor_window.h
    ../src/ui/export/image_export_controller.cpp
    ../src/ui/export/image_export_controller.h
    ../src/ui/export/image_export_worker.cpp
    ../src/ui/export/image_export_worker.h
    ../src/ui/dialogs/new_canvas_dialog.cpp
    ../src/ui/dialogs/new_canvas_dialog.h
    ../src/ui/dialogs/canvas_size_dialog.cpp
    ../src/ui/dialogs/canvas_size_dialog.h
    ../src/ui/dialogs/image_export_dialog.cpp
    ../src/ui/dialogs/image_export_dialog.h
    ../src/ui/dialogs/shortcut_settings_dialog.cpp
    ../src/ui/dialogs/shortcut_settings_dialog.h
    ../src/ui/canvas/image_canvas.cpp
    ../src/ui/canvas/image_canvas.h
    ../src/ui/tools/brush/brush_tool.cpp
    ../src/ui/tools/brush/brush_tool.h
    ../src/ui/tools/brush/paint_tool.cpp
    ../src/ui/tools/brush/paint_tool.h
    ../src/ui/tools/brush/eraser_tool.cpp
    ../src/ui/tools/brush/eraser_tool.h
    ../src/ui/tools/crop/crop_tool.cpp
    ../src/ui/tools/crop/crop_tool.h
    ../src/ui/tools/selection/area_selection_tool.cpp
    ../src/ui/tools/selection/area_selection_tool.h
    ../src/ui/tools/selection/object/object_selection_tool.cpp
    ../src/ui/tools/selection/object/object_selection_tool.h
    ../src/ui/tools/shapes/shape_tool.cpp
    ../src/ui/tools/shapes/shape_tool.h
    ../src/ui/tools/text/text_tool.cpp
    ../src/ui/tools/text/text_tool.h
    ../src/ui/tools/tool_sidebar.cpp
    ../src/ui/tools/tool_sidebar.h
    ../src/ui/layers/layer_panel.cpp
    ../src/ui/layers/layer_panel.h
)
target_include_directories(creative-suite-image-editor-ui-tests PRIVATE
    ../src/ui/canvas
    ../src/ui/dialogs
    ../src/ui/export
    ../src/ui/windows
    ../src/ui/tools
    ../src/ui/layers
)
target_link_libraries(creative-suite-image-editor-ui-tests PRIVATE
    creative-suite-image-editor-core
    creative-suite::shortcuts
    Qt6::Widgets
    Qt6::Test
)
target_compile_features(creative-suite-image-editor-ui-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-image-editor-mask-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-image-editor-core-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-image-editor-ui-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-image-editor-mask-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-image-editor-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-image-editor-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-image-editor-ui
    COMMAND creative-suite-image-editor-ui-tests)
set_tests_properties(creative-suite-image-editor-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(WIN32)
    target_link_libraries(creative-suite-image-editor-ui-tests PRIVATE user32)
    add_creative_suite_test(
        NAME creative-suite-image-editor-native-text
        COMMAND creative-suite-image-editor-ui-tests --text-layout-only)
    set_tests_properties(creative-suite-image-editor-native-text PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows"
    )
endif()

add_executable(creative-suite-image-editor-export-ui-tests
    image_editor_export_ui_test.cpp
    ../src/ui/windows/image_editor_window.cpp
    ../src/ui/windows/image_editor_window.h
    ../src/ui/export/image_export_controller.cpp
    ../src/ui/export/image_export_controller.h
    ../src/ui/export/image_export_worker.cpp
    ../src/ui/export/image_export_worker.h
    ../src/ui/dialogs/new_canvas_dialog.cpp
    ../src/ui/dialogs/new_canvas_dialog.h
    ../src/ui/dialogs/canvas_size_dialog.cpp
    ../src/ui/dialogs/canvas_size_dialog.h
    ../src/ui/dialogs/shortcut_settings_dialog.cpp
    ../src/ui/dialogs/shortcut_settings_dialog.h
    ../src/ui/dialogs/image_export_dialog.cpp
    ../src/ui/dialogs/image_export_dialog.h
    ../src/ui/canvas/image_canvas.cpp
    ../src/ui/canvas/image_canvas.h
    ../src/ui/tools/brush/brush_tool.cpp
    ../src/ui/tools/brush/brush_tool.h
    ../src/ui/tools/brush/paint_tool.cpp
    ../src/ui/tools/brush/paint_tool.h
    ../src/ui/tools/brush/eraser_tool.cpp
    ../src/ui/tools/brush/eraser_tool.h
    ../src/ui/tools/crop/crop_tool.cpp
    ../src/ui/tools/crop/crop_tool.h
    ../src/ui/tools/selection/area_selection_tool.cpp
    ../src/ui/tools/selection/area_selection_tool.h
    ../src/ui/tools/selection/object/object_selection_tool.cpp
    ../src/ui/tools/selection/object/object_selection_tool.h
    ../src/ui/tools/shapes/shape_tool.cpp
    ../src/ui/tools/shapes/shape_tool.h
    ../src/ui/tools/text/text_tool.cpp
    ../src/ui/tools/text/text_tool.h
    ../src/ui/tools/tool_sidebar.cpp
    ../src/ui/tools/tool_sidebar.h
    ../src/ui/layers/layer_panel.cpp
    ../src/ui/layers/layer_panel.h
)
target_include_directories(creative-suite-image-editor-export-ui-tests PRIVATE
    ../src/ui/canvas
    ../src/ui/dialogs
    ../src/ui/export
    ../src/ui/windows
    ../src/ui/tools
    ../src/ui/layers
)
target_link_libraries(creative-suite-image-editor-export-ui-tests PRIVATE
    creative-suite-image-editor-core
    creative-suite::shortcuts
    Qt6::Widgets
    Qt6::Test
)
target_compile_features(creative-suite-image-editor-export-ui-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-image-editor-export-ui-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-image-editor-export-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_creative_suite_test(
    NAME creative-suite-image-editor-export-ui
    COMMAND creative-suite-image-editor-export-ui-tests)
set_tests_properties(creative-suite-image-editor-export-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(WIN32)
    get_target_property(QT_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_TEST_CORE_DLL)
        get_target_property(QT_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_TEST_BIN_DIR "${QT_TEST_CORE_DLL}" DIRECTORY)
    find_program(QT_TEST_DEPLOY_TOOL windeployqt HINTS "${QT_TEST_BIN_DIR}" REQUIRED)

    foreach(TEST_TARGET IN ITEMS
            creative-suite-image-editor-core-tests
            creative-suite-image-editor-mask-tests
            creative-suite-image-editor-raster-tests
            creative-suite-image-editor-format-tests
            creative-suite-image-editor-ui-tests
            creative-suite-image-editor-export-ui-tests)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_TEST_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                --add-plugin-types imageformats
                --include-plugins qoffscreen
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
        creative_suite_deploy_qt_imageformat_plugins(${TEST_TARGET})
    endforeach()
endif()

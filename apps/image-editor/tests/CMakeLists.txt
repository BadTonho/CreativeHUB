add_executable(creative-suite-image-editor-core-tests
    image_editor_core_test.cpp
)

target_link_libraries(creative-suite-image-editor-core-tests PRIVATE
    creative-suite-image-editor-core
    Qt6::Test
)
target_compile_features(creative-suite-image-editor-core-tests PRIVATE cxx_std_20)

add_test(NAME creative-suite-image-editor-core COMMAND creative-suite-image-editor-core-tests)

add_executable(creative-suite-image-editor-format-tests
    image_editor_format_test.cpp
)
target_link_libraries(creative-suite-image-editor-format-tests PRIVATE
    Qt6::Core
    Qt6::Gui
)
target_compile_features(creative-suite-image-editor-format-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-image-editor-image-formats
    COMMAND creative-suite-image-editor-format-tests)
set_tests_properties(creative-suite-image-editor-image-formats PROPERTIES SKIP_RETURN_CODE 77)

add_executable(creative-suite-image-editor-ui-tests
    image_editor_ui_test.cpp
    ../src/ui/windows/image_editor_window.cpp
    ../src/ui/windows/image_editor_window.h
    ../src/ui/dialogs/new_canvas_dialog.cpp
    ../src/ui/dialogs/new_canvas_dialog.h
    ../src/ui/canvas/image_canvas.cpp
    ../src/ui/canvas/image_canvas.h
)
target_include_directories(creative-suite-image-editor-ui-tests PRIVATE
    ../src/ui/canvas
    ../src/ui/dialogs
    ../src/ui/windows
)
target_link_libraries(creative-suite-image-editor-ui-tests PRIVATE
    creative-suite-image-editor-core
    Qt6::Widgets
    Qt6::Test
)
target_compile_features(creative-suite-image-editor-ui-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-image-editor-core-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-image-editor-ui-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-image-editor-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-image-editor-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME creative-suite-image-editor-ui COMMAND creative-suite-image-editor-ui-tests)
set_tests_properties(creative-suite-image-editor-ui PROPERTIES
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
            creative-suite-image-editor-format-tests
            creative-suite-image-editor-ui-tests)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_TEST_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                --add-plugin-types imageformats
                --include-plugins qoffscreen
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

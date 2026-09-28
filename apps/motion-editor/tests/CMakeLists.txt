add_executable(creative-suite-motion-editor-document-tests
    composition_document_test.cpp
)
target_link_libraries(creative-suite-motion-editor-document-tests PRIVATE
    creative-suite-motion-editor-core
)
target_compile_features(creative-suite-motion-editor-document-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-motion-editor-document
    COMMAND creative-suite-motion-editor-document-tests)

add_executable(creative-suite-motion-editor-startup-tests
    motion_editor_startup_test.cpp
)
target_link_libraries(creative-suite-motion-editor-startup-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-startup-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-motion-editor-startup
    COMMAND creative-suite-motion-editor-startup-tests)
set_tests_properties(creative-suite-motion-editor-startup PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-motion-editor-ui-tests
    motion_editor_ui_test.cpp
)
target_link_libraries(creative-suite-motion-editor-ui-tests PRIVATE
    creative-suite-motion-editor-core
    Qt6::Widgets
)
target_compile_features(creative-suite-motion-editor-ui-tests PRIVATE cxx_std_20)
add_test(NAME creative-suite-motion-editor-ui
    COMMAND creative-suite-motion-editor-ui-tests)
set_tests_properties(creative-suite-motion-editor-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(MSVC)
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-motion-editor-document-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-motion-editor-ui-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_MOTION_TEST_CORE_DLL)
        get_target_property(QT_MOTION_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_MOTION_TEST_BIN_DIR "${QT_MOTION_TEST_CORE_DLL}" DIRECTORY)
    find_program(QT_MOTION_TEST_DEPLOY_TOOL windeployqt HINTS "${QT_MOTION_TEST_BIN_DIR}" REQUIRED)

    foreach(TEST_TARGET IN ITEMS
            creative-suite-motion-editor-startup-tests
            creative-suite-motion-editor-ui-tests)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND "${QT_MOTION_TEST_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                --include-plugins qoffscreen
                "$<TARGET_FILE:${TEST_TARGET}>"
            VERBATIM
        )
    endforeach()
endif()

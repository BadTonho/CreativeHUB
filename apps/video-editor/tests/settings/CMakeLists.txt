add_executable(creative-suite-main-editor-settings-dialog-tests
    settings_dialog_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/settings_dialog.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/settings_dialog.h
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/shortcut_manager.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/shortcut_manager.h
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/user_preferences.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/user_preferences.h
)

target_include_directories(creative-suite-main-editor-settings-dialog-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-settings-dialog-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-settings-dialog-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-settings-dialog-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-settings-dialog-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-settings-dialog-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-settings-dialog-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-settings-dialog-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-settings-dialog-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-settings-dialog
    COMMAND creative-suite-main-editor-settings-dialog-tests
)
set_tests_properties(creative-suite-main-editor-settings-dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)

add_executable(creative-suite-main-editor-shortcut-manager-tests
    shortcut_manager_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/shortcut_manager.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/settings/shortcut_manager.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/functions/function_palette.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/functions/function_palette.h
)

target_include_directories(creative-suite-main-editor-shortcut-manager-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-shortcut-manager-tests PRIVATE
    Qt6::Widgets
    Qt6::Test
)

target_compile_features(creative-suite-main-editor-shortcut-manager-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-shortcut-manager-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-shortcut-manager-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-shortcut-manager-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-shortcut-manager-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-shortcut-manager-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-shortcut-manager-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-shortcut-manager
    COMMAND creative-suite-main-editor-shortcut-manager-tests
)
set_tests_properties(creative-suite-main-editor-shortcut-manager PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

preserve_video_editor_test_working_directory()

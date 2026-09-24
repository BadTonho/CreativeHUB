include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-image-editor-core STATIC
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
    src/ui/windows/image_editor_window.cpp
    src/ui/windows/image_editor_window.h
    src/ui/dialogs/new_canvas_dialog.cpp
    src/ui/dialogs/new_canvas_dialog.h
    src/ui/dialogs/shortcut_settings_dialog.cpp
    src/ui/dialogs/shortcut_settings_dialog.h
    src/ui/canvas/image_canvas.cpp
    src/ui/canvas/image_canvas.h
    src/ui/tools/tool_sidebar.cpp
    src/ui/tools/tool_sidebar.h
    src/ui/layers/layer_panel.cpp
    src/ui/layers/layer_panel.h
)

target_include_directories(creative-suite-image-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/canvas
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/layers
)

target_link_libraries(creative-suite-image-editor PRIVATE
    creative-suite-image-editor-core
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
    get_target_property(QT_IMAGE_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_IMAGE_CORE_DLL)
        get_target_property(QT_IMAGE_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_IMAGE_BIN_DIR "${QT_IMAGE_CORE_DLL}" DIRECTORY)
    find_program(QT_IMAGE_DEPLOY_TOOL windeployqt HINTS "${QT_IMAGE_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-image-editor POST_BUILD
        COMMAND "${QT_IMAGE_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            --add-plugin-types imageformats
            "$<TARGET_FILE:creative-suite-image-editor>"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-image-editor-core STATIC
    src/image_document_store.cpp
    src/image_document_store.h
    src/image_document_session.cpp
    src/image_document_session.h
    src/image_editor_logger.cpp
    src/image_editor_logger.h
    src/recovery_store.cpp
    src/recovery_store.h
)

target_include_directories(creative-suite-image-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(creative-suite-image-editor-core PUBLIC
    Qt6::Core
    Qt6::Gui
)

target_compile_features(creative-suite-image-editor-core PUBLIC cxx_std_20)

add_executable(creative-suite-image-editor
    src/main.cpp
    src/image_editor_window.cpp
    src/image_editor_window.h
    src/image_canvas.cpp
    src/image_canvas.h
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

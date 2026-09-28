include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-motion-editor-core STATIC
    src/model/composition_document.cpp
    src/model/composition_document.h
    src/ui/composition_viewer.cpp
    src/ui/composition_viewer.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/new_composition_dialog.cpp
    src/ui/new_composition_dialog.h
    src/ui/timeline_navigator.cpp
    src/ui/timeline_navigator.h
)
target_include_directories(creative-suite-motion-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(creative-suite-motion-editor-core PUBLIC
    creative-suite::animation
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

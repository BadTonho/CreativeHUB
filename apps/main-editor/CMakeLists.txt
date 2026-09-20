include(GNUInstallDirs)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets)

add_executable(creative-suite-main-editor
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)

target_link_libraries(creative-suite-main-editor PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(creative-suite-main-editor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

install(TARGETS creative-suite-main-editor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET creative-suite-main-editor
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# Make the Release executable directly runnable from the CMake build tree on
# Windows. The Qt location is discovered from the imported Qt target; no local
# developer path is stored in the project.
if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor POST_BUILD
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor>"
        VERBATIM
    )
endif()

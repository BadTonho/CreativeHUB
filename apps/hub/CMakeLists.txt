set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

add_library(creative-suite-hub-core STATIC
    src/model/app_status.h
    src/model/app_info.h
    src/model/app_info.cpp
    src/model/download_item.h
    src/model/app_catalog.h
    src/model/app_catalog.cpp
    src/diagnostics/hub_logger.h
    src/diagnostics/hub_logger.cpp
    src/application/app_launcher.h
    src/application/app_launcher.cpp
    src/ui/theme/hub_palette.h
    src/ui/theme/hub_palette.cpp
    src/ui/theme/hub_style.h
    src/ui/theme/hub_style.cpp
    src/ui/cards/app_status_badge.h
    src/ui/cards/app_status_badge.cpp
    src/ui/cards/download_progress_bar.h
    src/ui/cards/download_progress_bar.cpp
    src/ui/cards/app_card_widget.h
    src/ui/cards/app_card_widget.cpp
    src/ui/dialogs/app_details_dialog.h
    src/ui/dialogs/app_details_dialog.cpp
    src/ui/header/header_bar.h
    src/ui/header/header_bar.cpp
    src/ui/header/expandable_search_bar.h
    src/ui/header/expandable_search_bar.cpp
    src/ui/sidebar/sidebar_button.h
    src/ui/sidebar/sidebar_button.cpp
    src/ui/sidebar/sidebar_widget.h
    src/ui/sidebar/sidebar_widget.cpp
    src/ui/pages/apps_page.h
    src/ui/pages/apps_page.cpp
    src/ui/pages/updates_page.h
    src/ui/pages/updates_page.cpp
    src/ui/pages/settings_page.h
    src/ui/pages/settings_page.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
)

target_include_directories(creative-suite-hub-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(creative-suite-hub-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_features(creative-suite-hub-core PUBLIC cxx_std_20)

add_executable(creative-suite-hub
    src/main.cpp
    resources/hub_resources.qrc
)

target_link_libraries(creative-suite-hub PRIVATE
    creative-suite-hub-core
    Qt6::Widgets
)

target_compile_features(creative-suite-hub PRIVATE cxx_std_20)

set_target_properties(creative-suite-hub PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(WIN32)
    set(APP_ICON_FILE "${CMAKE_SOURCE_DIR}/docs/assets/app-icons/hub.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/windows_app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/hub_app_icon.rc"
        @ONLY
    )
    target_sources(creative-suite-hub PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/hub_app_icon.rc"
    )
endif()

if(MSVC)
    target_compile_options(creative-suite-hub-core PRIVATE /W4 /permissive-)
    target_compile_options(creative-suite-hub PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-hub-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(creative-suite-hub PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_HUB_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_HUB_CORE_DLL)
        get_target_property(QT_HUB_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_HUB_BIN_DIR "${QT_HUB_CORE_DLL}" DIRECTORY)
    find_program(QT_HUB_DEPLOY_TOOL windeployqt HINTS "${QT_HUB_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-hub POST_BUILD
        COMMAND "${QT_HUB_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-hub>"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

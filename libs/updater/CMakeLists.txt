set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Network Widgets)

add_library(creative-suite-updater STATIC
    src/release_catalog.cpp
    src/update_service.cpp
    src/update_dialog.cpp
    include/creative_suite/updater/release_catalog.h
    include/creative_suite/updater/update_service.h
    include/creative_suite/updater/update_dialog.h
)
add_library(creative-suite::updater ALIAS creative-suite-updater)
target_include_directories(creative-suite-updater PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(creative-suite-updater PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    creative-suite::diagnostics
)
target_compile_features(creative-suite-updater PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(creative-suite-updater PRIVATE
        CREATIVE_SUITE_UPDATER_WINDOWS=1
    )
endif()

if(MSVC)
    target_compile_options(creative-suite-updater PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-updater PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

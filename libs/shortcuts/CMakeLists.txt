find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui)

add_library(creative-suite-shortcuts STATIC
    src/shortcut_manager.cpp
    include/creative_suite/shortcuts/shortcut_manager.h
)
add_library(creative-suite::shortcuts ALIAS creative-suite-shortcuts)
target_include_directories(creative-suite-shortcuts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(creative-suite-shortcuts PUBLIC Qt6::Core Qt6::Gui)
target_compile_features(creative-suite-shortcuts PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-shortcuts PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-shortcuts PRIVATE -Wall -Wextra -Wpedantic)
endif()

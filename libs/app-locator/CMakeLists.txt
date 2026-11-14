find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)

add_library(creative-suite-app-locator STATIC
    src/app_locator.cpp
    include/creative_suite/app_locator/app_locator.h
)
add_library(creative-suite::app-locator ALIAS creative-suite-app-locator)
target_include_directories(creative-suite-app-locator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(creative-suite-app-locator PUBLIC Qt6::Core)
target_compile_features(creative-suite-app-locator PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-app-locator PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-app-locator PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)

add_library(creative-suite-translations STATIC
    src/language_preference.cpp
    src/translation_catalog.cpp
    src/translation_runtime.cpp
    include/creative_suite/translations/language_preference.h
    include/creative_suite/translations/translation_catalog.h
    include/creative_suite/translations/translation_runtime.h
)
add_library(creative-suite::translations ALIAS creative-suite-translations)

target_include_directories(creative-suite-translations PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(creative-suite-translations PUBLIC
    Qt6::Core
    creative-suite::diagnostics
)
target_compile_features(creative-suite-translations PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-translations PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-translations PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING OR BUILD_CREATIVE_HUB)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS LinguistTools)
endif()

if(BUILD_CREATIVE_HUB)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Network)
    add_library(creative-suite-translation-updater STATIC
        src/translation_update_service.cpp
        include/creative_suite/translations/translation_update_service.h
    )
    add_library(creative-suite::translation-updater ALIAS creative-suite-translation-updater)
    target_link_libraries(creative-suite-translation-updater PUBLIC
        creative-suite::translations
        Qt6::Network
    )
    target_compile_features(creative-suite-translation-updater PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(creative-suite-translation-updater PRIVATE /W4 /permissive-)
    else()
        target_compile_options(creative-suite-translation-updater PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

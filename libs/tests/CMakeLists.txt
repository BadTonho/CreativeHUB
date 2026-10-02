add_executable(creative-suite-animation-tests
    animation_test.cpp
)
target_link_libraries(creative-suite-animation-tests PRIVATE creative-suite::animation)
target_compile_features(creative-suite-animation-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-animation
    COMMAND creative-suite-animation-tests)

add_executable(creative-suite-composition-tests
    composition_test.cpp
)
target_link_libraries(creative-suite-composition-tests PRIVATE creative-suite::composition)
target_compile_features(creative-suite-composition-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-composition
    COMMAND creative-suite-composition-tests)

if(TARGET creative-suite::shortcuts)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Test Widgets)
    add_executable(creative-suite-shortcuts-tests shortcuts_test.cpp)
    target_link_libraries(creative-suite-shortcuts-tests PRIVATE
        creative-suite::shortcuts
        Qt6::Test
        Qt6::Widgets
    )
    target_compile_features(creative-suite-shortcuts-tests PRIVATE cxx_std_20)
    add_creative_suite_test(
        NAME creative-suite-shortcuts
        COMMAND creative-suite-shortcuts-tests)
    set_tests_properties(creative-suite-shortcuts PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_target_property(QT_SHORTCUT_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT QT_SHORTCUT_TEST_CORE_DLL)
            get_target_property(QT_SHORTCUT_TEST_CORE_DLL Qt6::Core IMPORTED_LOCATION)
        endif()
        get_filename_component(QT_SHORTCUT_TEST_BIN_DIR
            "${QT_SHORTCUT_TEST_CORE_DLL}" DIRECTORY)
        find_program(QT_SHORTCUT_TEST_DEPLOY_TOOL
            windeployqt HINTS "${QT_SHORTCUT_TEST_BIN_DIR}" REQUIRED)
        add_custom_command(TARGET creative-suite-shortcuts-tests POST_BUILD
            COMMAND "${QT_SHORTCUT_TEST_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                --include-plugins qoffscreen
                "$<TARGET_FILE:creative-suite-shortcuts-tests>"
            VERBATIM
        )
    endif()
endif()

if(TARGET creative-suite::composition-opengl)
    add_executable(creative-suite-composition-opengl-tests opengl_composition_test.cpp)
    target_link_libraries(creative-suite-composition-opengl-tests PRIVATE creative-suite::composition-opengl)
    add_creative_suite_test(NAME creative-suite-composition-opengl
        SKIP_RETURN_CODE 77
        COMMAND creative-suite-composition-opengl-tests)
    if(WIN32)
        set_tests_properties(creative-suite-composition-opengl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    elseif(APPLE)
        set_tests_properties(creative-suite-composition-opengl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
    else()
        set_tests_properties(creative-suite-composition-opengl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    if(WIN32)
        add_custom_command(TARGET creative-suite-composition-opengl-tests POST_BUILD
            COMMAND "${QT_SHORTCUT_TEST_DEPLOY_TOOL}"
                --no-translations --no-compiler-runtime
                "$<TARGET_FILE:creative-suite-composition-opengl-tests>"
            VERBATIM)
    endif()
endif()

foreach(shared_test IN ITEMS
        creative-suite-animation-tests
        creative-suite-composition-tests
        creative-suite-composition-opengl-tests
        creative-suite-shortcuts-tests)
    if(NOT TARGET ${shared_test})
        continue()
    endif()
    if(MSVC)
        target_compile_options(${shared_test} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${shared_test} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

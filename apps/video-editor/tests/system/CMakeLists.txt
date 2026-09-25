add_executable(creative-suite-main-editor-system-memory-tests
    system_memory_usage_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.h
)

target_include_directories(creative-suite-main-editor-system-memory-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(creative-suite-main-editor-system-memory-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-system-memory-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-system-memory-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-system-memory-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-system-memory
    COMMAND creative-suite-main-editor-system-memory-tests
)

add_executable(creative-suite-main-editor-performance-usage-tests
    performance_usage_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/performance_usage.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/performance_usage.h
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.h
)

target_include_directories(creative-suite-main-editor-performance-usage-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(creative-suite-main-editor-performance-usage-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-performance-usage-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(creative-suite-main-editor-performance-usage-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-performance-usage-tests PRIVATE cxx_std_20)

add_test(
    NAME creative-suite-main-editor-performance-usage
    COMMAND creative-suite-main-editor-performance-usage-tests
)

add_executable(creative-suite-main-editor-system-memory-details-tests
    system_memory_details_dialog_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/system/system_memory_usage.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/system/system_memory_details_dialog.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/system/system_memory_details_dialog.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/system/system_memory_indicator.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/system/system_memory_indicator.h
)

target_include_directories(creative-suite-main-editor-system-memory-details-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-system-memory-details-tests PRIVATE
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(creative-suite-main-editor-system-memory-details-tests PRIVATE Psapi)
endif()

target_compile_features(creative-suite-main-editor-system-memory-details-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-system-memory-details-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-system-memory-details-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION_RELEASE)
    if(NOT QT_GUI_DLL)
        get_target_property(QT_GUI_DLL Qt6::Gui IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-system-memory-details-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-system-memory-details-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-system-memory-details-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-system-memory-details-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-system-memory-details
    COMMAND creative-suite-main-editor-system-memory-details-tests
)
set_tests_properties(creative-suite-main-editor-system-memory-details PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

preserve_video_editor_test_working_directory()

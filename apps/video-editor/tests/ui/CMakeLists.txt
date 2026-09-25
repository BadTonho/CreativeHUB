add_executable(creative-suite-main-editor-media-browser-list-widget-tests
    media_browser_list_widget_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/main_window/main_window_support.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/main_window/main_window_support.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_browser_list_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_browser_list_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_drag_mime.h
)

target_include_directories(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-media-browser-list-widget-tests PRIVATE -Wall -Wextra -Wpedantic)
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

    add_custom_command(TARGET creative-suite-main-editor-media-browser-list-widget-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-list-widget-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-media-browser-list-widget-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-list-widget-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-media-browser-list-widget
    COMMAND creative-suite-main-editor-media-browser-list-widget-tests
)
set_tests_properties(creative-suite-main-editor-media-browser-list-widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-media-browser-bin-tree-tests
    media_browser_bin_tree_widget_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_browser_bin_tree_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_browser_bin_tree_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/media_browser/media_drag_mime.h
)

target_include_directories(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-media-browser-bin-tree-tests PRIVATE -Wall -Wextra -Wpedantic)
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

    add_custom_command(TARGET creative-suite-main-editor-media-browser-bin-tree-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-bin-tree-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-media-browser-bin-tree-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-media-browser-bin-tree-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-media-browser-bin-tree
    COMMAND creative-suite-main-editor-media-browser-bin-tree-tests
)
set_tests_properties(creative-suite-main-editor-media-browser-bin-tree PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-preview-tests
    preview_widget_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/preview/preview_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/preview/preview_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/opengl_preview_surface.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/opengl_preview_surface.h
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/rendering/preview_performance_metrics.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_frame.h
)

target_include_directories(creative-suite-main-editor-preview-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-preview-tests PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

target_compile_features(creative-suite-main-editor-preview-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-preview-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-preview-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()
    get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_WIDGETS_DLL)
        get_target_property(QT_WIDGETS_DLL Qt6::Widgets IMPORTED_LOCATION)
    endif()
    get_target_property(QT_OPENGL_DLL Qt6::OpenGL IMPORTED_LOCATION_RELEASE)
    if(NOT QT_OPENGL_DLL)
        get_target_property(QT_OPENGL_DLL Qt6::OpenGL IMPORTED_LOCATION)
    endif()
    get_target_property(QT_OPENGL_WIDGETS_DLL Qt6::OpenGLWidgets IMPORTED_LOCATION_RELEASE)
    if(NOT QT_OPENGL_WIDGETS_DLL)
        get_target_property(QT_OPENGL_WIDGETS_DLL Qt6::OpenGLWidgets IMPORTED_LOCATION)
    endif()

    get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
    find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

    add_custom_command(TARGET creative-suite-main-editor-preview-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_WIDGETS_DLL}"
            "${QT_OPENGL_DLL}"
            "${QT_OPENGL_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-preview-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-preview-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-preview-tests>/platforms"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-preview
    COMMAND creative-suite-main-editor-preview-tests
)
set_tests_properties(creative-suite-main-editor-preview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1"
)

add_executable(creative-suite-main-editor-workspace-page-switch-tests
    workspace_page_switch_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/timeline/timeline_end_buttons.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/timeline/timeline_end_buttons.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/workspace/workspace_host.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/workspace/workspace_host.h
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/workspace/workspace_page_id.h
)

target_include_directories(creative-suite-main-editor-workspace-page-switch-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-workspace-page-switch-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-workspace-page-switch-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-workspace-page-switch-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-workspace-page-switch-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME creative-suite-main-editor-workspace-page-switch
    COMMAND creative-suite-main-editor-workspace-page-switch-tests
)
set_tests_properties(creative-suite-main-editor-workspace-page-switch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

preserve_video_editor_test_working_directory()

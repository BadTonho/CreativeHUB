include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)
find_package(Qt6 CONFIG QUIET COMPONENTS Multimedia)
find_package(FFMPEG MODULE REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWRESAMPLE SWSCALE)

if(TARGET Qt6::Multimedia)
    set(CREATIVE_SUITE_HAS_QT_MULTIMEDIA TRUE)
else()
    set(CREATIVE_SUITE_HAS_QT_MULTIMEDIA FALSE)
    message(WARNING "Qt6::Multimedia was not found; audio output will use the diagnostic fallback.")
endif()

add_executable(creative-suite-main-editor
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/main_window/main_window_support.cpp
    src/main_window/main_window_support.h
    src/main_window/main_window_workspace.cpp
    src/main_window/main_window_effects.cpp
    src/main_window/main_window_project.cpp
    src/main_window/main_window_media.cpp
    src/main_window/main_window_timeline.cpp
    src/main_window/main_window_playback.cpp
    src/main_window/main_window_inspector.cpp
    src/settings/settings_dialog.cpp
    src/settings/settings_dialog.h
    src/settings/shortcut_manager.cpp
    src/settings/shortcut_manager.h
    src/settings/user_preferences.cpp
    src/settings/user_preferences.h
    src/system/system_memory_usage.cpp
    src/system/system_memory_usage.h
    src/preview_widget.cpp
    src/preview_widget.h
    src/rendering/opengl_preview_surface.cpp
    src/rendering/opengl_preview_surface.h
    src/rendering/frame_compositor.cpp
    src/rendering/frame_compositor.h
    src/rendering/preview_performance_metrics.cpp
    src/rendering/preview_performance_metrics.h
    src/rendering/text_renderer.cpp
    src/rendering/text_renderer.h
    src/playback/playback_worker.cpp
    src/playback/playback_worker.h
    src/playback/audio_output.cpp
    src/playback/audio_output.h
    src/logging/logger.cpp
    src/logging/logger.h
    src/media/video_decoder.cpp
    src/media/video_decoder.h
    src/media/video_frame.h
    src/media/audio_frame.h
    src/media/audio_playback.cpp
    src/media/audio_playback.h
    src/media/media_library.cpp
    src/media/media_library.h
    src/media/video_metadata.h
    src/media/video_playback.cpp
    src/media/video_playback.h
    src/media/video_probe.cpp
    src/media/video_probe.h
    src/project/project_document.h
    src/project/project_file.cpp
    src/project/project_file.h
    src/timeline/timeline_model.cpp
    src/timeline/timeline_model.h
    src/timeline/timeline_zoom.h
    src/timeline/timeline_transform.cpp
    src/timeline/timeline_transform.h
    src/timeline/timeline_history.cpp
    src/timeline/timeline_history.h
    src/timeline/timeline_widget.cpp
    src/timeline/timeline_widget.h
    src/ui/media_browser_list_widget.cpp
    src/ui/media_browser_list_widget.h
    src/ui/media_browser_bin_tree_widget.cpp
    src/ui/media_browser_bin_tree_widget.h
    src/ui/media_drag_mime.h
    src/ui/effects_catalog.cpp
    src/ui/effects_catalog.h
    src/ui/effects_favorites_widget.cpp
    src/ui/effects_favorites_widget.h
    src/ui/effects_toolbox_widget.cpp
    src/ui/effects_toolbox_widget.h
    src/ui/effects_list_widget.cpp
    src/ui/effects_list_widget.h
    src/ui/system_memory_details_dialog.cpp
    src/ui/system_memory_details_dialog.h
    src/ui/system_memory_indicator.cpp
    src/ui/system_memory_indicator.h
)

target_link_libraries(creative-suite-main-editor PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${FFMPEG_LIBRARIES}
)

if(WIN32)
    target_link_libraries(creative-suite-main-editor PRIVATE Psapi)
endif()

if(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    target_link_libraries(creative-suite-main-editor PRIVATE Qt6::Multimedia)
    target_compile_definitions(creative-suite-main-editor PRIVATE CREATIVE_SUITE_HAS_QT_MULTIMEDIA=1)
endif()

target_include_directories(creative-suite-main-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(creative-suite-main-editor PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_compile_features(creative-suite-main-editor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
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

    set(MAIN_EDITOR_POST_BUILD_COMMANDS
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor>"
    )

    set(FFMPEG_RUNTIME_DLLS)
    foreach(FFMPEG_LIBRARY_DIR IN LISTS FFMPEG_LIBRARY_DIRS)
        if(FFMPEG_LIBRARY_DIR MATCHES "[/\\\\]debug([/\\\\]|$)")
            continue()
        endif()
        get_filename_component(FFMPEG_ROOT_DIR "${FFMPEG_LIBRARY_DIR}" DIRECTORY)
        foreach(FFMPEG_RUNTIME_NAME IN ITEMS avcodec avdevice avfilter avformat avutil swresample swscale)
            file(GLOB FFMPEG_RUNTIME_MATCHES
                "${FFMPEG_ROOT_DIR}/bin/${FFMPEG_RUNTIME_NAME}-*.dll")
            list(APPEND FFMPEG_RUNTIME_DLLS ${FFMPEG_RUNTIME_MATCHES})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES FFMPEG_RUNTIME_DLLS)

    if(FFMPEG_RUNTIME_DLLS)
        install(FILES ${FFMPEG_RUNTIME_DLLS}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        list(APPEND MAIN_EDITOR_POST_BUILD_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${FFMPEG_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:creative-suite-main-editor>"
        )
    endif()

    add_custom_command(TARGET creative-suite-main-editor POST_BUILD
        ${MAIN_EDITOR_POST_BUILD_COMMANDS}
        VERBATIM
    )
endif()

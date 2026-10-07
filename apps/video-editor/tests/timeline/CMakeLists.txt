add_executable(creative-suite-main-editor-timeline-tests
    timeline_model_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_clip_edge_command.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_clip_edge_command.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_history.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_history.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-timeline-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline
    COMMAND creative-suite-main-editor-timeline-tests
)

add_executable(creative-suite-main-editor-timeline-command-service-tests
    timeline_command_service_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/application/editor_session.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/application/editor_session.h
    ${VIDEO_EDITOR_SOURCE_DIR}/application/media_controller.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/application/media_controller.h
    ${VIDEO_EDITOR_SOURCE_DIR}/application/timeline_command_service.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/application/timeline_command_service.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_clip_edge_command.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_clip_edge_command.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_history.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_history.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-command-service-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-timeline-command-service-tests PRIVATE
    creative-suite::media-assets
)

target_compile_features(creative-suite-main-editor-timeline-command-service-tests PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-main-editor-timeline-command-service-tests)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-command-service-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-command-service-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-command-service
    COMMAND creative-suite-main-editor-timeline-command-service-tests
)

add_executable(creative-suite-main-editor-node-graph-tests
    node_graph_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.h
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/evaluation/node_graph_evaluator.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/evaluation/node_graph_evaluator.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
)
target_include_directories(creative-suite-main-editor-node-graph-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR})
target_link_libraries(creative-suite-main-editor-node-graph-tests PRIVATE
    creative_suite::animation creative_suite::effects creative_suite::composition)
target_compile_features(creative-suite-main-editor-node-graph-tests PRIVATE cxx_std_20)
add_creative_suite_test(
    NAME creative-suite-main-editor-node-graph
    COMMAND creative-suite-main-editor-node-graph-tests)

add_executable(creative-suite-main-editor-timeline-trim-gesture-tests
    timeline_trim_gesture_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.h
)

target_include_directories(creative-suite-main-editor-timeline-trim-gesture-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_compile_features(creative-suite-main-editor-timeline-trim-gesture-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-trim-gesture-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-trim-gesture-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-trim-gesture
    COMMAND creative-suite-main-editor-timeline-trim-gesture-tests
)

add_executable(creative-suite-main-editor-timeline-widget-tests
    timeline_widget_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/audio_waveform.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_time.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_layout.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_geometry.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_geometry.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_drop_validation.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_drop_validation.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_painter.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_painter.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_controller.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_controller.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_track_header_overlay.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_track_header_overlay.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_widget.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_widget.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-widget-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-timeline-widget-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-timeline-widget-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-widget-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-widget-tests PRIVATE -Wall -Wextra -Wpedantic)
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

    add_custom_command(TARGET creative-suite-main-editor-timeline-widget-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-timeline-widget-tests>"
        COMMAND "${QT_DEPLOY_TOOL}"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:creative-suite-main-editor-timeline-widget-tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/$<IF:$<CONFIG:Debug>,qoffscreend.dll,qoffscreen.dll>"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-timeline-widget-tests>/platforms"
        VERBATIM
    )
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-widget
    COMMAND creative-suite-main-editor-timeline-widget-tests
)
set_tests_properties(creative-suite-main-editor-timeline-widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(creative-suite-main-editor-timeline-geometry-tests
    timeline_geometry_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/video_metadata.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_geometry.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_geometry.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_drop_validation.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_drop_validation.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_painter.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_painter.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-geometry-tests PRIVATE ${VIDEO_EDITOR_SOURCE_DIR})
target_link_libraries(creative-suite-main-editor-timeline-geometry-tests PRIVATE Qt6::Gui)
target_compile_features(creative-suite-main-editor-timeline-geometry-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-geometry-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-geometry
    COMMAND creative-suite-main-editor-timeline-geometry-tests
)

add_executable(creative-suite-main-editor-timeline-interaction-controller-tests
    timeline_interaction_controller_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_controller.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_interaction_controller.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_trim_gesture.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_model.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-timeline-interaction-controller-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-timeline-interaction-controller-tests PRIVATE
    Qt6::Core
    Qt6::Gui
)

target_compile_features(creative-suite-main-editor-timeline-interaction-controller-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-interaction-controller-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-interaction-controller-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-interaction-controller
    COMMAND creative-suite-main-editor-timeline-interaction-controller-tests
)

add_executable(creative-suite-main-editor-timeline-end-buttons-tests
    timeline_end_buttons_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/timeline/timeline_end_buttons.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/ui/timeline/timeline_end_buttons.h
)

target_include_directories(creative-suite-main-editor-timeline-end-buttons-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-timeline-end-buttons-tests PRIVATE
    Qt6::Widgets
)

target_compile_features(creative-suite-main-editor-timeline-end-buttons-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-timeline-end-buttons-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-timeline-end-buttons-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-timeline-end-buttons
    COMMAND creative-suite-main-editor-timeline-end-buttons-tests
)
set_tests_properties(creative-suite-main-editor-timeline-end-buttons PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

preserve_video_editor_test_working_directory()

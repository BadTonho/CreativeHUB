add_executable(creative-suite-main-editor-project-tests
    project_file_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_layout.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_detail.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_reader.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_writer.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document_validator.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/workspaces/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-project-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/motion-handoff/include
)

target_link_libraries(creative-suite-main-editor-project-tests PRIVATE
    Qt6::Core
    creative-suite::media-assets
)

target_compile_features(creative-suite-main-editor-project-tests PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-main-editor-project-tests)
deploy_qt_test_runtime(creative-suite-main-editor-project-tests)

if(MSVC)
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-project
    COMMAND creative-suite-main-editor-project-tests
)

add_executable(creative-suite-main-editor-autosave-tests
    autosave_manager_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/logging/logger.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/autosave_manager.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/autosave_manager.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_detail.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_reader.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_writer.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document_validator.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/workspaces/fusion/nodes/model/node_graph.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_layout.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-autosave-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-autosave-tests PRIVATE
    Qt6::Core
    creative-suite::media-assets
)

target_compile_features(creative-suite-main-editor-autosave-tests PRIVATE cxx_std_20)
creative_suite_deploy_media_runtime(creative-suite-main-editor-autosave-tests)
deploy_qt_test_runtime(creative-suite-main-editor-autosave-tests)

if(MSVC)
    target_compile_options(creative-suite-main-editor-autosave-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-autosave-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_creative_suite_test(
    NAME creative-suite-main-editor-autosave
    COMMAND creative-suite-main-editor-autosave-tests
)

preserve_video_editor_test_working_directory()

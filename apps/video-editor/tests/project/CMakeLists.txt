add_executable(creative-suite-main-editor-project-tests
    project_file_test.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document.h
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_layout.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_detail.h
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_reader.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_file_writer.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document_validator.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.cpp
    ${VIDEO_EDITOR_SOURCE_DIR}/timeline/timeline_transform.h
)

target_include_directories(creative-suite-main-editor-project-tests PRIVATE
    ${VIDEO_EDITOR_SOURCE_DIR}
)

target_link_libraries(creative-suite-main-editor-project-tests PRIVATE
    Qt6::Core
)

target_compile_features(creative-suite-main-editor-project-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-project-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()

    add_custom_command(TARGET creative-suite-main-editor-project-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-project-tests>"
        VERBATIM
    )
endif()

add_test(
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
    ${VIDEO_EDITOR_SOURCE_DIR}/project/project_document.h
    ${VIDEO_EDITOR_SOURCE_DIR}/media/media_library.cpp
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
)

target_compile_features(creative-suite-main-editor-autosave-tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(creative-suite-main-editor-autosave-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(creative-suite-main-editor-autosave-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_DLL)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
    endif()

    add_custom_command(TARGET creative-suite-main-editor-autosave-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_CORE_DLL}"
            "$<TARGET_FILE_DIR:creative-suite-main-editor-autosave-tests>"
        VERBATIM
    )
endif()

add_test(
    NAME creative-suite-main-editor-autosave
    COMMAND creative-suite-main-editor-autosave-tests
)

preserve_video_editor_test_working_directory()

set(VIDEO_EDITOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(VIDEO_EDITOR_TEST_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(REFERENCE_VIDEO "${PROJECT_SOURCE_DIR}/prototypes/testdata/reference.mkv")

macro(preserve_video_editor_test_working_directory)
    get_property(VIDEO_EDITOR_GROUP_TESTS DIRECTORY PROPERTY TESTS)
    if(VIDEO_EDITOR_GROUP_TESTS)
        set_tests_properties(${VIDEO_EDITOR_GROUP_TESTS} PROPERTIES
            WORKING_DIRECTORY "${VIDEO_EDITOR_TEST_BINARY_DIR}"
        )
    endif()
endmacro()

function(copy_ffmpeg_test_runtime target_name)
    if(WIN32)
        set(FFMPEG_TEST_RUNTIME_DLLS)
        foreach(FFMPEG_LIBRARY_DIR IN LISTS FFMPEG_LIBRARY_DIRS)
            if(FFMPEG_LIBRARY_DIR MATCHES "[/\\\\]debug([/\\\\]|$)")
                continue()
            endif()
            get_filename_component(FFMPEG_ROOT_DIR "${FFMPEG_LIBRARY_DIR}" DIRECTORY)
            foreach(FFMPEG_RUNTIME_NAME IN ITEMS avcodec avdevice avfilter avformat avutil swresample swscale)
                file(GLOB FFMPEG_RUNTIME_MATCHES
                    "${FFMPEG_ROOT_DIR}/bin/${FFMPEG_RUNTIME_NAME}-*.dll")
                list(APPEND FFMPEG_TEST_RUNTIME_DLLS ${FFMPEG_RUNTIME_MATCHES})
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES FFMPEG_TEST_RUNTIME_DLLS)

        if(FFMPEG_TEST_RUNTIME_DLLS)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${FFMPEG_TEST_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM
            )
        endif()
    endif()
endfunction()

function(deploy_qt_test_runtime target_name)
    if(WIN32)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT QT_CORE_DLL)
            get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
        endif()
        get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
        find_program(QT_DEPLOY_TOOL windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${QT_DEPLOY_TOOL}"
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${target_name}>"
            VERBATIM
        )
    endif()
endfunction()

add_subdirectory(application)
add_subdirectory(effects)
add_subdirectory(logging)
add_subdirectory(media)
add_subdirectory(playback)
add_subdirectory(project)
add_subdirectory(rendering)
add_subdirectory(settings)
add_subdirectory(system)
add_subdirectory(timeline)
add_subdirectory(ui)

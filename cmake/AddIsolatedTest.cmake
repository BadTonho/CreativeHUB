function(add_creative_suite_test)
    cmake_parse_arguments(PARSE_ARGV 0 TEST "" "NAME;SKIP_RETURN_CODE" "COMMAND")

    if(NOT TEST_NAME OR NOT TEST_COMMAND)
        message(FATAL_ERROR "add_creative_suite_test requires NAME and COMMAND")
    endif()
    if(TEST_UNPARSED_ARGUMENTS OR TEST_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid add_creative_suite_test arguments: ${TEST_UNPARSED_ARGUMENTS}")
    endif()

    set(_test_command ${TEST_COMMAND})
    list(POP_FRONT _test_command _test_target)
    if(TARGET "${_test_target}")
        set(_test_executable "$<TARGET_FILE:${_test_target}>")
    else()
        set(_test_executable "${_test_target}")
    endif()

    set(_test_argument_definitions)
    set(_test_argument_count 0)
    foreach(_test_argument IN LISTS _test_command)
        list(APPEND _test_argument_definitions
            "-DTEST_ARGUMENT_${_test_argument_count}=${_test_argument}")
        math(EXPR _test_argument_count "${_test_argument_count} + 1")
    endforeach()

    set(_test_wrapper_arguments)
    if(TEST_SKIP_RETURN_CODE)
        list(APPEND _test_wrapper_arguments
            "-DTEST_SKIP_RETURN_CODE=${TEST_SKIP_RETURN_CODE}")
    endif()

    add_test(NAME "${TEST_NAME}"
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXECUTABLE=${_test_executable}"
            "-DTEST_TEMP_ROOT=${CMAKE_BINARY_DIR}/ctest-temp"
            "-DTEST_NAME=${TEST_NAME}"
            "-DTEST_ARGUMENT_COUNT=${_test_argument_count}"
            ${_test_argument_definitions}
            ${_test_wrapper_arguments}
            -P "${PROJECT_SOURCE_DIR}/cmake/run_test_with_temp_dir.cmake")

    if(TEST_SKIP_RETURN_CODE)
        set_tests_properties("${TEST_NAME}" PROPERTIES
            SKIP_REGULAR_EXPRESSION "CREATIVE_SUITE_TEST_SKIPPED")
    endif()
endfunction()

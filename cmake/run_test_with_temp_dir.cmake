if(NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED TEST_TEMP_ROOT OR NOT DEFINED TEST_LOG_ROOT OR
   NOT DEFINED TEST_NAME OR NOT DEFINED TEST_TIMEOUT_SECONDS OR
   NOT DEFINED TEST_DEBUG_DIAGNOSTICS OR NOT DEFINED TEST_ARGUMENT_COUNT)
    message(FATAL_ERROR "TEST_EXECUTABLE, TEST_TEMP_ROOT, TEST_LOG_ROOT, TEST_NAME, TEST_TIMEOUT_SECONDS, TEST_DEBUG_DIAGNOSTICS, and TEST_ARGUMENT_COUNT are required")
endif()

file(MAKE_DIRECTORY "${TEST_TEMP_ROOT}")
file(MAKE_DIRECTORY "${TEST_LOG_ROOT}")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run_id)
set(_test_temp_dir "${TEST_TEMP_ROOT}/${_run_id}")
while(EXISTS "${_test_temp_dir}")
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run_id)
    set(_test_temp_dir "${TEST_TEMP_ROOT}/${_run_id}")
endwhile()
file(MAKE_DIRECTORY "${_test_temp_dir}")
if(NOT IS_DIRECTORY "${_test_temp_dir}")
    message(FATAL_ERROR "Could not create isolated test temp directory: ${_test_temp_dir}")
endif()

set(_test_arguments)
if(TEST_ARGUMENT_COUNT GREATER 0)
    math(EXPR _last_test_argument "${TEST_ARGUMENT_COUNT} - 1")
    foreach(_argument_index RANGE 0 ${_last_test_argument})
        if(NOT DEFINED TEST_ARGUMENT_${_argument_index})
            message(FATAL_ERROR "Missing TEST_ARGUMENT_${_argument_index}")
        endif()
        list(APPEND _test_arguments "${TEST_ARGUMENT_${_argument_index}}")
    endforeach()
endif()

set(_test_environment)
if(TEST_DEBUG_DIAGNOSTICS)
    list(APPEND _test_environment "QT_DEBUG_PLUGINS=1")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        ${_test_environment}
        "TEMP=${_test_temp_dir}"
        "TMP=${_test_temp_dir}"
        "TMPDIR=${_test_temp_dir}"
        "${TEST_EXECUTABLE}" ${_test_arguments}
    TIMEOUT "${TEST_TIMEOUT_SECONDS}"
    RESULT_VARIABLE _test_result
    OUTPUT_VARIABLE _test_stdout
    ERROR_VARIABLE _test_stderr
)

set(_test_log "${TEST_LOG_ROOT}/${TEST_NAME}-${_run_id}.log")
file(WRITE "${_test_log}"
    "Test: ${TEST_NAME}\nResult: ${_test_result}\n\n--- stdout ---\n${_test_stdout}\n--- stderr ---\n${_test_stderr}\n")

# Some Qt objects keep files open until static destruction. Remove the isolated
# directory after the test process exits so Windows has released every handle.
file(REMOVE_RECURSE "${_test_temp_dir}")
if(EXISTS "${_test_temp_dir}")
    message(FATAL_ERROR "Could not remove isolated test temp directory: ${_test_temp_dir}")
endif()

if(DEFINED TEST_SKIP_RETURN_CODE AND
   "${_test_result}" STREQUAL "${TEST_SKIP_RETURN_CODE}")
    message("CREATIVE_SUITE_TEST_SKIPPED: ${TEST_NAME}\n${_test_stdout}${_test_stderr}")
    return()
endif()

if(NOT "${_test_result}" STREQUAL "0")
    message(STATUS "Test output captured in ${_test_log}")
    if(NOT "${_test_stdout}" STREQUAL "")
        message(STATUS "Test stdout:\n${_test_stdout}")
    endif()
    if(NOT "${_test_stderr}" STREQUAL "")
        message(STATUS "Test stderr:\n${_test_stderr}")
    endif()
    message(FATAL_ERROR "Test ${TEST_NAME} failed with result: ${_test_result}")
endif()

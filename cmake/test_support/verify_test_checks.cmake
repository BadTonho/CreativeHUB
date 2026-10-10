if(NOT DEFINED PROBE_EXECUTABLE OR NOT EXISTS "${PROBE_EXECUTABLE}")
    message(FATAL_ERROR "Test check probe executable is missing: ${PROBE_EXECUTABLE}")
endif()

execute_process(COMMAND "${PROBE_EXECUTABLE}"
    RESULT_VARIABLE success_result OUTPUT_VARIABLE success_output ERROR_VARIABLE success_error
    TIMEOUT 10)
if(NOT "${success_result}" STREQUAL "0")
    message(FATAL_ERROR "Checks did not evaluate their condition exactly once with NDEBUG: ${success_result}\n${success_output}\n${success_error}")
endif()

execute_process(COMMAND "${PROBE_EXECUTABLE}" fail
    RESULT_VARIABLE failure_result OUTPUT_VARIABLE failure_output ERROR_VARIABLE failure_error
    TIMEOUT 10)
if(NOT "${failure_result}" STREQUAL "1")
    message(FATAL_ERROR "A deliberate failed check must exit with code 1 under NDEBUG: ${failure_result}\n${failure_output}\n${failure_error}")
endif()
if(NOT failure_error MATCHES "test_check_probe\\.cpp:[0-9]+: check failed: false")
    message(FATAL_ERROR "The failed check did not report its source location and condition: ${failure_error}")
endif()

message(STATUS "Checks evaluate once and deliberate failures are detected with NDEBUG")

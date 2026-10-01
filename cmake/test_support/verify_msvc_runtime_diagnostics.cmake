if(NOT DEFINED PROBE_EXECUTABLE)
    message(FATAL_ERROR "PROBE_EXECUTABLE is required")
endif()

foreach(_mode IN ITEMS rtc crt-assert)
    execute_process(
        COMMAND "${PROBE_EXECUTABLE}" "${_mode}"
        TIMEOUT 10
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    set(_output "${_stdout}\n${_stderr}")

    if("${_result}" STREQUAL "0")
        message(FATAL_ERROR "The ${_mode} probe unexpectedly succeeded:\n${_output}")
    endif()
    if("${_result}" MATCHES "timeout")
        message(FATAL_ERROR "The ${_mode} probe timed out instead of reporting a diagnostic:\n${_output}")
    endif()
    if(NOT _output MATCHES "CREATIVE_SUITE_RUNTIME_DIAGNOSTIC")
        message(FATAL_ERROR "The ${_mode} probe did not emit its diagnostic marker:\n${_output}")
    endif()
    if(NOT _output MATCHES "module=[^\r\n]+")
        message(FATAL_ERROR "The ${_mode} diagnostic did not identify its module:\n${_output}")
    endif()

    if(_mode STREQUAL "rtc")
        if(NOT _output MATCHES "file=[^\r\n]+line=[0-9]+")
            message(FATAL_ERROR "The RTC diagnostic did not include source file and line:\n${_output}")
        endif()
    else()
        if(NOT _output MATCHES "msvc_runtime_diagnostics_probe.cpp[(][0-9]+[)]")
            message(FATAL_ERROR "The CRT diagnostic did not include its source file and line:\n${_output}")
        endif()
    endif()
endforeach()

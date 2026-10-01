if(NOT DEFINED TEST_CONFIGURATION OR NOT DEFINED QT_DEPLOY_TOOL OR
   NOT DEFINED QT_TARGET_FILE)
    message(FATAL_ERROR "TEST_CONFIGURATION, QT_DEPLOY_TOOL, and QT_TARGET_FILE are required")
endif()

if(NOT TEST_CONFIGURATION STREQUAL "Debug")
    return()
endif()

execute_process(
    COMMAND "${QT_DEPLOY_TOOL}"
        --no-translations
        --no-compiler-runtime
        --add-plugin-types imageformats
        "${QT_TARGET_FILE}"
    RESULT_VARIABLE _qt_deploy_result
    OUTPUT_VARIABLE _qt_deploy_stdout
    ERROR_VARIABLE _qt_deploy_stderr
)
if(NOT "${_qt_deploy_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Qt deployment for ${QT_TARGET_FILE} failed with ${_qt_deploy_result}:\n${_qt_deploy_stdout}\n${_qt_deploy_stderr}")
endif()

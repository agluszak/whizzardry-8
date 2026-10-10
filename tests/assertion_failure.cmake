execute_process(COMMAND "${PROGRAM}" "--assert-${MODE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(result STREQUAL "0" OR result MATCHES "timeout")
    message(FATAL_ERROR "Assertion did not fail promptly: ${result}\n${output}\n${error}")
endif()
if(NOT error MATCHES "Assertion failed:|invalid buttonID")
    message(FATAL_ERROR "Missing assertion diagnostic: ${result}\n${output}\n${error}")
endif()

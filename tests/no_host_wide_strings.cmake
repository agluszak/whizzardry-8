execute_process(COMMAND "${NM}" -P -u ${BINARIES}
    RESULT_VARIABLE result OUTPUT_VARIABLE symbols ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect native symbols: ${error}")
endif()
if(symbols MATCHES "(^|\n)_?(wcs|wmem|swprintf|vswprintf|wprintf)[^ \n]*[ \t]+U")
    string(REGEX MATCHALL "(^|\n)_?(wcs|wmem|swprintf|vswprintf|wprintf)[^ \n]*[ \t]+U[^\n]*" imports "${symbols}")
    message(FATAL_ERROR "First-party module imports host wide-string functions: ${imports}")
endif()

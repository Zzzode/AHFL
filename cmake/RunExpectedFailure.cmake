if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED EXPECTED_REGEX)
    message(FATAL_ERROR "EXPECTED_REGEX is required")
endif()

if(DEFINED AHFLC_ARGS)
    set(ahflc_command "${AHFLC}")
    foreach(arg IN LISTS AHFLC_ARGS)
        list(APPEND ahflc_command "${arg}")
    endforeach()
else()
    set(ahflc_command "${AHFLC}" check "${INPUT_FILE}")
endif()

execute_process(
    COMMAND ${ahflc_command}
    RESULT_VARIABLE ahflc_result
    OUTPUT_VARIABLE ahflc_stdout
    ERROR_VARIABLE ahflc_stderr
)

string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")

if(ahflc_result EQUAL 0)
    message(FATAL_ERROR
        "expected ahflc check to fail for '${INPUT_FILE}', but it succeeded\n${ahflc_output}"
    )
endif()

if(NOT ahflc_output MATCHES "${EXPECTED_REGEX}")
    message(FATAL_ERROR
        "expected output for '${INPUT_FILE}' to match regex '${EXPECTED_REGEX}'\n${ahflc_output}"
    )
endif()

message(STATUS "${ahflc_output}")

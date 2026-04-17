if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is not defined")
endif()

if(NOT DEFINED AHFLC_ARGS)
    message(FATAL_ERROR "AHFLC_ARGS is not defined")
endif()

if(NOT DEFINED EXPECTED_FILE)
    message(FATAL_ERROR "EXPECTED_FILE is not defined")
endif()

separate_arguments(ahflc_args UNIX_COMMAND "${AHFLC_ARGS}")

execute_process(
    COMMAND "${AHFLC}" ${ahflc_args}
    RESULT_VARIABLE result_code
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
)

if(NOT result_code EQUAL 0)
    message(FATAL_ERROR
        "command failed with exit code ${result_code}\n"
        "stderr:\n${actual_error}"
    )
endif()

file(READ "${EXPECTED_FILE}" expected_output)

string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch\n"
        "expected:\n${expected_output}\n"
        "actual:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is not defined")
endif()

if(NOT DEFINED SUBCOMMAND)
    message(FATAL_ERROR "SUBCOMMAND is not defined")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is not defined")
endif()

if(NOT DEFINED EXPECTED_FILE)
    message(FATAL_ERROR "EXPECTED_FILE is not defined")
endif()

execute_process(
    COMMAND "${AHFLC}" "${SUBCOMMAND}" "${INPUT_FILE}"
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
        "output mismatch for ${SUBCOMMAND} ${INPUT_FILE}\n"
        "expected:\n${expected_output}\n"
        "actual:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

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

set(ahflc_args)
if(DEFINED AHFLC_ARGS AND NOT AHFLC_ARGS STREQUAL "")
    separate_arguments(ahflc_args UNIX_COMMAND "${AHFLC_ARGS}")
endif()

separate_arguments(subcommand_args UNIX_COMMAND "${SUBCOMMAND}")

execute_process(
    COMMAND "${AHFLC}" ${subcommand_args} ${ahflc_args} "${INPUT_FILE}"
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

# Optional: normalize positional JSON node "id" fields before comparing. See
# RunExpectedCommandOutput.cmake for rationale (project IR-JSON embeds stdlib
# IR, so node ids shift on stdlib growth; ids are structural, not semantic).
if(DEFINED NORMALIZE_IDS AND NORMALIZE_IDS)
    string(REGEX REPLACE "\"id\": [0-9]+" "\"id\": 0" actual_output "${actual_output}")
    string(REGEX REPLACE "\"id\": [0-9]+" "\"id\": 0" expected_output "${expected_output}")
endif()

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch for ${SUBCOMMAND} ${INPUT_FILE}\n"
        "expected:\n${expected_output}\n"
        "actual:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

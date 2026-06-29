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

# Optional: normalize positional JSON node "id" fields before comparing. Project
# IR-JSON embeds stdlib module IR, so the node ids shift whenever stdlib grows;
# the ids are structural (not semantic), so normalizing them to 0 — like a
# snapshot test normalizes pointers/timestamps — eliminates that golden churn
# without touching the (refactor-entangled) ir_json.cpp. See workplan §3.5 #5.
if(DEFINED NORMALIZE_IDS AND NORMALIZE_IDS)
    string(REGEX REPLACE "\"id\": [0-9]+" "\"id\": 0" actual_output "${actual_output}")
    string(REGEX REPLACE "\"id\": [0-9]+" "\"id\": 0" expected_output "${expected_output}")
endif()

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch\n"
        "expected:\n${expected_output}\n"
        "actual:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is not defined")
endif()

if(NOT DEFINED AHFLC_ARGS)
    message(FATAL_ERROR "AHFLC_ARGS is not defined")
endif()

if(NOT DEFINED EXPECTED_REGEX)
    message(FATAL_ERROR "EXPECTED_REGEX is not defined")
endif()

set(ahflc_command "${AHFLC}")
foreach(arg IN LISTS AHFLC_ARGS)
    list(APPEND ahflc_command "${arg}")
endforeach()

if(DEFINED WORKING_DIRECTORY AND NOT WORKING_DIRECTORY STREQUAL "")
    execute_process(
        COMMAND ${ahflc_command}
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE actual_output
        ERROR_VARIABLE actual_error
    )
else()
    execute_process(
        COMMAND ${ahflc_command}
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE actual_output
        ERROR_VARIABLE actual_error
    )
endif()

string(CONCAT combined_output "${actual_output}" "${actual_error}")

if(NOT result_code EQUAL 0)
    message(FATAL_ERROR
        "command failed with exit code ${result_code}\n"
        "stdout:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

if(NOT combined_output MATCHES "${EXPECTED_REGEX}")
    message(FATAL_ERROR
        "expected command output to match regex '${EXPECTED_REGEX}'\n"
        "stdout:\n${actual_output}\n"
        "stderr:\n${actual_error}"
    )
endif()

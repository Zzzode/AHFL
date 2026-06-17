if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED MAX_BYTES)
    message(FATAL_ERROR "MAX_BYTES is required")
endif()

if(NOT DEFINED MAX_LINES)
    message(FATAL_ERROR "MAX_LINES is required")
endif()

if(NOT DEFINED MAX_LTLSPEC)
    message(FATAL_ERROR "MAX_LTLSPEC is required")
endif()

if(NOT DEFINED MIN_LTLSPEC)
    set(MIN_LTLSPEC 1)
endif()

execute_process(
    COMMAND "${AHFLC}" emit smv "${INPUT_FILE}"
    RESULT_VARIABLE result_code
    OUTPUT_VARIABLE smv_output
    ERROR_VARIABLE command_error
)

if(NOT result_code EQUAL 0)
    message(FATAL_ERROR
        "emit smv failed with exit code ${result_code}\n"
        "stderr:\n${command_error}"
    )
endif()

string(LENGTH "${smv_output}" byte_count)
string(REGEX MATCHALL "\n" line_breaks "${smv_output}")
list(LENGTH line_breaks line_count)
string(LENGTH "${smv_output}" smv_output_length)
if(smv_output_length GREATER 0)
    math(EXPR last_index "${smv_output_length} - 1")
    string(SUBSTRING "${smv_output}" ${last_index} 1 last_char)
endif()
if(smv_output_length GREATER 0 AND NOT last_char STREQUAL "\n")
    math(EXPR line_count "${line_count} + 1")
endif()

string(REGEX MATCHALL "(^|\n)LTLSPEC" ltlspec_matches "${smv_output}")
list(LENGTH ltlspec_matches ltlspec_count)

if(byte_count GREATER MAX_BYTES)
    message(FATAL_ERROR
        "SMV output exceeded byte budget for ${INPUT_FILE}: "
        "${byte_count} > ${MAX_BYTES}"
    )
endif()

if(line_count GREATER MAX_LINES)
    message(FATAL_ERROR
        "SMV output exceeded line budget for ${INPUT_FILE}: "
        "${line_count} > ${MAX_LINES}"
    )
endif()

if(ltlspec_count GREATER MAX_LTLSPEC)
    message(FATAL_ERROR
        "SMV output exceeded LTLSPEC budget for ${INPUT_FILE}: "
        "${ltlspec_count} > ${MAX_LTLSPEC}"
    )
endif()

if(ltlspec_count LESS MIN_LTLSPEC)
    message(FATAL_ERROR
        "SMV output emitted too few LTLSPEC entries for ${INPUT_FILE}: "
        "${ltlspec_count} < ${MIN_LTLSPEC}"
    )
endif()

message(STATUS
    "SMV budget ok: bytes=${byte_count}/${MAX_BYTES}, "
    "lines=${line_count}/${MAX_LINES}, "
    "ltlspec=${ltlspec_count}/${MAX_LTLSPEC}"
)

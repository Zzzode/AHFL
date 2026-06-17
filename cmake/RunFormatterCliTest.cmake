if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED MODE)
    message(FATAL_ERROR "MODE is required")
endif()

if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required")
endif()

if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(work_file "${WORK_DIR}/input.ahfl")
file(COPY_FILE "${SOURCE_FILE}" "${work_file}")

if(DEFINED CONFIG_FILE)
    file(COPY_FILE "${CONFIG_FILE}" "${WORK_DIR}/.ahfl-format")
endif()

function(assert_file_matches EXPECTED_PATH ACTUAL_PATH)
    file(READ "${ACTUAL_PATH}" actual_content)
    file(READ "${EXPECTED_PATH}" expected_content)
    if(NOT actual_content STREQUAL expected_content)
        message(FATAL_ERROR "formatted file did not match expected: ${ACTUAL_PATH}\nactual:\n${actual_content}\nexpected:\n${expected_content}")
    endif()
endfunction()

function(seed_formatter_tree ROOT_DIR)
    file(MAKE_DIRECTORY "${ROOT_DIR}/src/nested")
    file(COPY_FILE "${SOURCE_FILE}" "${ROOT_DIR}/src/a.ahfl")
    file(COPY_FILE "${SOURCE_FILE}" "${ROOT_DIR}/src/nested/b.ahfl")
endfunction()

function(assert_formatter_tree_matches_expected ROOT_DIR)
    if(NOT DEFINED EXPECTED_FILE)
        message(FATAL_ERROR "EXPECTED_FILE is required for this formatter test mode")
    endif()
    assert_file_matches("${EXPECTED_FILE}" "${ROOT_DIR}/src/a.ahfl")
    assert_file_matches("${EXPECTED_FILE}" "${ROOT_DIR}/src/nested/b.ahfl")
endfunction()

function(assert_formatter_tree_unchanged ROOT_DIR)
    assert_file_matches("${SOURCE_FILE}" "${ROOT_DIR}/src/a.ahfl")
    assert_file_matches("${SOURCE_FILE}" "${ROOT_DIR}/src/nested/b.ahfl")
endfunction()

function(write_formatter_project_descriptors ROOT_DIR)
    file(WRITE "${ROOT_DIR}/ahfl.project.json"
"{
  \"format_version\": \"ahfl.project.v0.3\",
  \"name\": \"fmt-project\",
  \"search_roots\": [\".\"],
  \"entry_sources\": [\"src/a.ahfl\", \"src/nested/b.ahfl\"]
}
")
    file(WRITE "${ROOT_DIR}/ahfl.workspace.json"
"{
  \"format_version\": \"ahfl.workspace.v0.3\",
  \"name\": \"fmt-workspace\",
  \"projects\": [\"ahfl.project.json\"]
}
")
endfunction()

if(MODE STREQUAL "format")
    execute_process(
        COMMAND "${AHFLC}" fmt "${work_file}"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    if(NOT ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected fmt to succeed\n${ahflc_stdout}${ahflc_stderr}")
    endif()

    if(NOT DEFINED EXPECTED_FILE)
        message(FATAL_ERROR "EXPECTED_FILE is required for MODE=format")
    endif()
    assert_file_matches("${EXPECTED_FILE}" "${work_file}")
elseif(MODE STREQUAL "check-pass")
    execute_process(
        COMMAND "${AHFLC}" fmt --check "${work_file}"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    if(NOT ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected fmt --check to succeed\n${ahflc_stdout}${ahflc_stderr}")
    endif()
elseif(MODE STREQUAL "check-fail")
    execute_process(
        COMMAND "${AHFLC}" fmt --check "${work_file}"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")
    if(ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected fmt --check to fail\n${ahflc_output}")
    endif()
    if(DEFINED EXPECTED_REGEX AND NOT ahflc_output MATCHES "${EXPECTED_REGEX}")
        message(FATAL_ERROR "expected fmt --check output to match '${EXPECTED_REGEX}'\n${ahflc_output}")
    endif()

    file(READ "${work_file}" actual)
    file(READ "${SOURCE_FILE}" original)
    if(NOT actual STREQUAL original)
        message(FATAL_ERROR "fmt --check changed the input file")
    endif()
elseif(MODE STREQUAL "format-directory")
    seed_formatter_tree("${WORK_DIR}")
    execute_process(
        COMMAND "${AHFLC}" fmt "${WORK_DIR}/src"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")
    if(NOT ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected directory fmt to succeed\n${ahflc_output}")
    endif()
    if(NOT ahflc_output MATCHES "ok: formatted 2 file\\(s\\), 2 changed")
        message(FATAL_ERROR "expected directory fmt summary\n${ahflc_output}")
    endif()
    assert_formatter_tree_matches_expected("${WORK_DIR}")
elseif(MODE STREQUAL "check-directory-fail")
    seed_formatter_tree("${WORK_DIR}")
    execute_process(
        COMMAND "${AHFLC}" fmt --check "${WORK_DIR}/src"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")
    if(ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected directory fmt --check to fail\n${ahflc_output}")
    endif()
    if(NOT ahflc_output MATCHES "format check failed for 2 of 2 file\\(s\\)")
        message(FATAL_ERROR "expected directory fmt --check failure count\n${ahflc_output}")
    endif()
    if(NOT ahflc_output MATCHES "format failed for 2 of 2 input\\(s\\)")
        message(FATAL_ERROR "expected directory fmt partial failure summary\n${ahflc_output}")
    endif()
    assert_formatter_tree_unchanged("${WORK_DIR}")
elseif(MODE STREQUAL "format-project")
    seed_formatter_tree("${WORK_DIR}")
    write_formatter_project_descriptors("${WORK_DIR}")
    execute_process(
        COMMAND "${AHFLC}" fmt --project "${WORK_DIR}/ahfl.project.json"
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")
    if(NOT ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected project fmt to succeed\n${ahflc_output}")
    endif()
    if(NOT ahflc_output MATCHES "ok: formatted 2 file\\(s\\), 2 changed")
        message(FATAL_ERROR "expected project fmt summary\n${ahflc_output}")
    endif()
    assert_formatter_tree_matches_expected("${WORK_DIR}")
elseif(MODE STREQUAL "format-workspace")
    seed_formatter_tree("${WORK_DIR}")
    write_formatter_project_descriptors("${WORK_DIR}")
    execute_process(
        COMMAND "${AHFLC}" fmt --workspace "${WORK_DIR}/ahfl.workspace.json" --project-name fmt-project
        RESULT_VARIABLE ahflc_result
        OUTPUT_VARIABLE ahflc_stdout
        ERROR_VARIABLE ahflc_stderr
    )

    string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")
    if(NOT ahflc_result EQUAL 0)
        message(FATAL_ERROR "expected workspace fmt to succeed\n${ahflc_output}")
    endif()
    if(NOT ahflc_output MATCHES "ok: formatted 2 file\\(s\\), 2 changed")
        message(FATAL_ERROR "expected workspace fmt summary\n${ahflc_output}")
    endif()
    assert_formatter_tree_matches_expected("${WORK_DIR}")
else()
    message(FATAL_ERROR "unknown formatter CLI test mode: ${MODE}")
endif()

message(STATUS "${ahflc_stdout}${ahflc_stderr}")

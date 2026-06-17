if(NOT DEFINED CONFIG_FILE)
    message(FATAL_ERROR "CONFIG_FILE is required")
endif()

if(NOT DEFINED REPORT_FILE)
    message(FATAL_ERROR "REPORT_FILE is required")
endif()

if(NOT DEFINED MIN_TARGETS)
    set(MIN_TARGETS 1)
endif()

if(NOT DEFINED MIN_TEST_SUITES)
    set(MIN_TEST_SUITES 1)
endif()

if(NOT DEFINED MIN_MUTATORS)
    set(MIN_MUTATORS 1)
endif()

file(READ "${CONFIG_FILE}" mutation_config)

string(JSON tool GET "${mutation_config}" mutation_testing tool)
string(JSON version GET "${mutation_config}" mutation_testing version)
string(JSON report_format GET "${mutation_config}" mutation_testing report_format)
string(JSON target_count LENGTH "${mutation_config}" mutation_testing targets)
string(JSON suite_count LENGTH "${mutation_config}" mutation_testing test_suites)
string(JSON mutator_count LENGTH "${mutation_config}" mutation_testing mutators)

if(NOT tool STREQUAL "mull")
    message(FATAL_ERROR "Expected mutation tool 'mull', got '${tool}'")
endif()

if(version STREQUAL "")
    message(FATAL_ERROR "Mutation tool version must be declared")
endif()

if(NOT report_format STREQUAL "json")
    message(FATAL_ERROR "Mutation report_format must be json")
endif()

if(target_count LESS MIN_TARGETS)
    message(FATAL_ERROR
        "Mutation target count below budget: ${target_count} < ${MIN_TARGETS}"
    )
endif()

if(suite_count LESS MIN_TEST_SUITES)
    message(FATAL_ERROR
        "Mutation test suite count below budget: ${suite_count} < ${MIN_TEST_SUITES}"
    )
endif()

if(mutator_count LESS MIN_MUTATORS)
    message(FATAL_ERROR
        "Mutation mutator count below budget: ${mutator_count} < ${MIN_MUTATORS}"
    )
endif()

get_filename_component(report_dir "${REPORT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${report_dir}")
file(WRITE "${REPORT_FILE}" "{\n")
file(APPEND "${REPORT_FILE}" "  \"status\": \"config_report\",\n")
file(APPEND "${REPORT_FILE}" "  \"tool\": \"${tool}\",\n")
file(APPEND "${REPORT_FILE}" "  \"version\": \"${version}\",\n")
file(APPEND "${REPORT_FILE}" "  \"runner\": \"not_executed\",\n")
file(APPEND "${REPORT_FILE}" "  \"mutation_score\": null,\n")
file(APPEND "${REPORT_FILE}" "  \"targets\": ${target_count},\n")
file(APPEND "${REPORT_FILE}" "  \"test_suites\": ${suite_count},\n")
file(APPEND "${REPORT_FILE}" "  \"mutators\": ${mutator_count}\n")
file(APPEND "${REPORT_FILE}" "}\n")

message(STATUS
    "Mutation config report ok: tool=${tool} ${version}, "
    "targets=${target_count}, test_suites=${suite_count}, "
    "mutators=${mutator_count}, report=${REPORT_FILE}"
)

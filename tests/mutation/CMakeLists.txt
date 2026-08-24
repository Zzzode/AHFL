# Mutation testing configuration
# Run with: ./run_mutation.sh from this directory
# Requires: mull-runner or custom mutation framework

# This CMakeLists.txt serves as documentation for the mutation testing setup
# Actual mutation testing is driven by run_mutation.sh

# Copy config to build directory for convenience
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/mutation_config.json
    ${CMAKE_CURRENT_BINARY_DIR}/mutation_config.json
    COPYONLY
)

add_test(NAME ahfl.mutation.config_report
    COMMAND ${CMAKE_COMMAND}
            "-DCONFIG_FILE=${CMAKE_CURRENT_LIST_DIR}/mutation_config.json"
            "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mutation_reports/config-report.json"
            "-DMIN_TARGETS=3"
            "-DMIN_TEST_SUITES=3"
            "-DMIN_MUTATORS=9"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunMutationConfigGate.cmake"
)
set_tests_properties(ahfl.mutation.config_report PROPERTIES
    PASS_REGULAR_EXPRESSION "Mutation config report ok"
)

# Self-contained fallback mutation runner: applies a fixed, deterministic set
# of source mutants to COPIES of a tiny target and records a real
# killed/survived score, with NO dependency on mull. The gate validates the
# JSON score report is produced and well-formed; it does not gate on an
# absolute score threshold (which would be flaky).
add_test(NAME ahfl.mutation.fallback_score
    COMMAND ${CMAKE_COMMAND}
            "-DRUNNER=${CMAKE_CURRENT_LIST_DIR}/run_fallback_mutation.sh"
            "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mutation_reports/fallback-score.json"
            "-DMIN_MUTANTS=4"
            -P "${CMAKE_CURRENT_LIST_DIR}/RunFallbackMutationGate.cmake"
)
set_tests_properties(ahfl.mutation.fallback_score PROPERTIES
    PASS_REGULAR_EXPRESSION "Mutation fallback score report ok"
    LABELS "ahfl-v0.59;v0.59-mutation;v0.59-quality-gates"
)

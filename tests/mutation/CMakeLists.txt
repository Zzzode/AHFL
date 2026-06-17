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

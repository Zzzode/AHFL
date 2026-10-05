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
# of source mutants to COPIES of three tiny representative targets (integer
# arithmetic, a canonical structured writer, and an agent-style state machine)
# and records a real killed/survived score, with NO dependency on mull.
#
# The gate is release-blocking, not merely structural: it pins every mutant's
# outcome to the `expect` recorded in the target's committed spec
# (fallback/<target>/<target>.target.json) and blocks when a target's score
# falls below its committed score_floor. The specs are the single source of
# truth, so adding a representative target or recording a reviewed intentional
# survivor is a spec edit, never a gate or runner edit (KR7.3).
add_test(NAME ahfl.mutation.fallback_score
    COMMAND ${CMAKE_COMMAND}
            "-DRUNNER=${CMAKE_CURRENT_LIST_DIR}/run_fallback_mutation.sh"
            "-DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mutation_reports/fallback-score.json"
            "-DSPEC_ROOT=${CMAKE_CURRENT_LIST_DIR}/fallback"
            -P "${CMAKE_CURRENT_LIST_DIR}/RunFallbackMutationGate.cmake"
)
set_tests_properties(ahfl.mutation.fallback_score PROPERTIES
    PASS_REGULAR_EXPRESSION "Mutation fallback score report ok"
    # The gate fails via a non-zero exit (FATAL_ERROR), so the pass pattern
    # alone already makes a breach fail. These two tokens additionally make
    # ctest fail if the gate ever returns 0 while signalling a fixture fault;
    # they are single tokens because ctest matches each output LINE, and a
    # multi-word phrase could be wrapped mid-line.
    FAIL_REGULAR_EXPRESSION "FIXTURE BROKEN|CMake Error"
    LABELS "mutation;quality-gates"
)

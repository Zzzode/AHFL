# InfraTargetTests.cmake -- P2 infra-target backlog item (issue-backlog §3.6).
#
# Snapshot + structural-validation + determinism gate for the three infra
# emit targets (k8s-crd, openapi, terraform). See
# tests/scripts/infra_target_gate.py for what each target asserts.
#
# The infra backends are compiled only when AHFL_ENABLE_BACKEND_INFRA is ON
# (root CMakeLists.txt option). Register the gate only in that case so the
# suite stays green when infra backends are configured out.
#
# NOTE: this file is included from tests/CMakeLists.txt (the orchestrator
# wires the include() line). It relies on AHFL_TESTS_DIR and Python3::Interpreter
# being set up by the parent list file.

if(AHFL_ENABLE_BACKEND_INFRA)
    add_test(NAME ahflc.emit_infra.target_gate
        COMMAND ${Python3_EXECUTABLE}
                "${AHFL_TESTS_DIR}/scripts/infra_target_gate.py"
                $<TARGET_FILE:ahflc>
                "${AHFL_TESTS_DIR}"
    )
    set_tests_properties(ahflc.emit_infra.target_gate PROPERTIES
        PASS_REGULAR_EXPRESSION "all infra target gates passed"
        FAIL_REGULAR_EXPRESSION "FAIL:|NON-DETERMINISTIC|mismatch"
        LABELS "infra;golden;backend;target"
    )
endif()

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

    # RFC 0019 slice 4/5: emit wasm --profile wasi|browser. The wasi (default)
    # and browser profiles both emit the shared module ABI + ahfl_cap imports;
    # an unknown profile is a usage error.
    add_test(NAME ahflc.emit_wasm.profile_wasi
        COMMAND $<TARGET_FILE:ahflc> emit wasm --wasm-profile wasi
                "${AHFL_TESTS_DIR}/golden/formal/ok_smt_encoding.ahfl"
    )
    set_tests_properties(ahflc.emit_wasm.profile_wasi PROPERTIES
        PASS_REGULAR_EXPRESSION "\\(export \"run\"\\)"
        LABELS "wasm;backend;target"
    )

    add_test(NAME ahflc.emit_wasm.profile_browser
        COMMAND $<TARGET_FILE:ahflc> emit wasm --wasm-profile browser
                "${AHFL_TESTS_DIR}/golden/formal/ok_smt_encoding.ahfl"
    )
    set_tests_properties(ahflc.emit_wasm.profile_browser PROPERTIES
        PASS_REGULAR_EXPRESSION "\\(export \"run\"\\)"
        LABELS "wasm;backend;target"
    )

    add_test(NAME ahflc.emit_wasm.profile_unknown
        COMMAND $<TARGET_FILE:ahflc> emit wasm --wasm-profile bogus
                "${AHFL_TESTS_DIR}/golden/formal/ok_smt_encoding.ahfl"
    )
    set_tests_properties(ahflc.emit_wasm.profile_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --wasm-profile"
        LABELS "wasm;backend;target"
    )
endif()

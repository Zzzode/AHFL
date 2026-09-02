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

    # RFC 0026 KR6.5 E1: CLI now emits one deterministic Core-IR-derived wasm
    # binary, not textual WAT. This always-on gate is explicitly structural;
    # the optional wasmtime differential below owns real-execution evidence.
    add_test(NAME ahflc.emit_wasm.e1_binary_gate
        COMMAND ${Python3_EXECUTABLE}
                "${AHFL_TESTS_DIR}/scripts/wasm_e1_binary_gate.py"
                $<TARGET_FILE:ahflc>
                "${AHFL_TESTS_DIR}"
    )
    set_tests_properties(ahflc.emit_wasm.e1_binary_gate PROPERTIES
        PASS_REGULAR_EXPRESSION "all KR6.5 E1 binary/structural gates passed"
        FAIL_REGULAR_EXPRESSION "FAIL:|NON-DETERMINISTIC"
        LABELS "wasm;backend;target"
    )

    # KR6.5 E2: exact ahfl_cap import + run2 binary contract. This gate parses
    # bytes but does not claim execution evidence.
    add_test(NAME ahflc.emit_wasm.e2_binary_gate
        COMMAND ${Python3_EXECUTABLE}
                "${AHFL_TESTS_DIR}/scripts/wasm_e2_binary_gate.py"
                $<TARGET_FILE:ahflc>
                "${AHFL_TESTS_DIR}"
    )
    set_tests_properties(ahflc.emit_wasm.e2_binary_gate PROPERTIES
        PASS_REGULAR_EXPRESSION "all KR6.5 E2 binary/structural gates passed"
        FAIL_REGULAR_EXPRESSION "FAIL:|NON-DETERMINISTIC"
        LABELS "wasm;backend;target;structural"
    )

    add_test(NAME ahflc.emit_wasm.profile_unknown
        COMMAND $<TARGET_FILE:ahflc> emit wasm --wasm-profile bogus
                "${AHFL_TESTS_DIR}/golden/wasm/e1_identity_agent.ahfl"
    )
    set_tests_properties(ahflc.emit_wasm.profile_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --wasm-profile"
        LABELS "wasm;backend;target"
    )

    # RFC 0026 E4-B2-C: capability-workflow binary/structural gate. Drives the real
    # ahflc package entry (committed manifest isolated into a TemporaryDirectory
    # with a copy of the committed golden), asserts byte identity vs the emit-only
    # producer + determinism, and locks the import / AHFLXM-golden / AHFLWS-EOF
    # structure. Not execution evidence.
    add_test(NAME ahflc.emit_wasm.capability_workflow_binary_gate
        COMMAND ${Python3_EXECUTABLE}
                "${AHFL_TESTS_DIR}/scripts/wasm_workflow_cap_binary_gate.py"
                $<TARGET_FILE:ahflc>
                $<TARGET_FILE:ahfl_core_wasm_capability_workflow_probe>
                "${AHFL_TESTS_DIR}/integration/wasm_capability_workflow/ahfl.toml"
                "${AHFL_TESTS_DIR}/golden/wasm/e3_capability_workflow.ahfl"
                "${AHFL_TESTS_DIR}/integration/wasm_e3_workflow/ahfl.toml"
    )
    set_tests_properties(ahflc.emit_wasm.capability_workflow_binary_gate PROPERTIES
        PASS_REGULAR_EXPRESSION
            "all KR6.5 E4-B2-C capability-workflow binary/structural gates passed"
        FAIL_REGULAR_EXPRESSION "FAIL:|NON-DETERMINISTIC"
        LABELS "wasm;backend;target;structural"
    )
endif()

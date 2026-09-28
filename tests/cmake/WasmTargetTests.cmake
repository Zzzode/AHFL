# WasmTargetTests.cmake -- CLI structural gates for the executable wasm
# backend (RFC 0026 KR6.5 E1/E2, P6-7 frame section, profile diagnostics,
# E4-B2-C capability workflow). Every gate here parses emitted bytes or
# checks deterministic structure; real-execution evidence is owned by the
# optional wasmtime/Node ctests in ProjectTests.cmake.
#
# The executable wasm backend is compiled only when AHFL_ENABLE_BACKEND_WASM
# is ON (root CMakeLists.txt option). Register the gates only in that case.
#
# NOTE: this file is included from tests/CMakeLists.txt (the orchestrator
# wires the include() line). It relies on AHFL_TESTS_DIR and Python3::Interpreter
# being set up by the parent list file.

if(AHFL_ENABLE_BACKEND_WASM)
    # RFC 0026 KR6.5 E1: CLI emits one deterministic Core-IR-derived wasm
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

    # RFC 0026 P6-7 rung A fix-forward: the core-layout frame section is
    # eligible ONLY for the pinned frame-lane agents (no capability import, no
    # outlined fn/closure). Emits every golden fixture and fails if any
    # non-pinned module gains (or any pinned module loses) the frame section, or
    # if an emission is non-deterministic. Locks the rung-A byte-identity
    # invariant across the whole corpus, not just the e1 fixture.
    add_test(NAME ahflc.emit_wasm.p67_frame_section_eligibility_gate
        COMMAND ${Python3_EXECUTABLE}
                "${AHFL_TESTS_DIR}/scripts/wasm_frame_section_eligibility_gate.py"
                $<TARGET_FILE:ahflc>
                "${AHFL_TESTS_DIR}"
    )
    set_tests_properties(ahflc.emit_wasm.p67_frame_section_eligibility_gate PROPERTIES
        PASS_REGULAR_EXPRESSION "all P6-7 frame-section eligibility gates passed"
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

set(AHFL_EXAMPLES_DIR "${PROJECT_SOURCE_DIR}/examples")
set(AHFL_TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
set(AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE
    "${AHFL_TESTS_DIR}/integration/package_golden/ok_expr_temporal/ir/expr_temporal.ahfl"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME ahfl.architecture.boundaries
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-architecture.py"
)

# RFC 0026 Objective 6A (KR6.4-pre): WASM toolchain preflight. Three tiers:
#  1. fixture integrity — the committed preflight.wasm matches its generator
#     (byte-exact, auditable, no wat2wasm dependency); always runs.
#  2. decision-logic unit test — version parsing, minimum gate, discovery
#     precedence, and skip-vs-fail policy via a fake wasmtime shim; always runs,
#     needs no real wasmtime (this is what keeps the gate honest on CI images
#     without the toolchain).
#  3. real-execution smoke — ALWAYS registered so a missing toolchain surfaces
#     as a visible ctest SKIP (exit 77) rather than a silently absent test.
#     Discovery provenance is preserved: the EXPLICIT path (AHFL_WASMTIME) and
#     the PATH-discovered path (AHFL_DETECTED_WASMTIME) are forwarded under
#     separate flags so the harness owns the skip-vs-fail decision.
add_test(NAME ahfl.wasm.preflight_fixture_integrity
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/fixtures/wasm/make_preflight_fixture.py"
            --check
)

add_test(NAME ahfl.wasm.preflight_decision_logic
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_preflight_test.py"
)

# find_program leaves AHFL_DETECTED_WASMTIME as `<var>-NOTFOUND` when absent;
# normalize that sentinel to an empty string so the harness sees "nothing
# discovered" rather than a bogus path.
set(_ahfl_detected_wasmtime "${AHFL_DETECTED_WASMTIME}")
if(NOT AHFL_DETECTED_WASMTIME)
    set(_ahfl_detected_wasmtime "")
endif()

add_test(NAME ahfl.wasm.preflight_execution
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_preflight.py"
            --explicit-wasmtime "${AHFL_WASMTIME}"
            --detected-wasmtime "${_ahfl_detected_wasmtime}"
)
set_tests_properties(ahfl.wasm.preflight_execution PROPERTIES SKIP_RETURN_CODE 77)

if(AHFL_ENABLE_BACKEND_INFRA)
    # KR6.5 E1: this producer always runs one checked frontend program through
    # native AgentRuntime and AHFL->Core->layout->wasm. It is a semantic/binary
    # probe but not, by itself, real wasm execution evidence.
    add_test(NAME ahfl.wasm.e1_same_frontend_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_e1_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e1_identity_agent.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/e1_same_frontend_probe.wasm"
    )
    set_tests_properties(ahfl.wasm.e1_same_frontend_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "final_state_id=0 transition_count=1 identity_output=1"
        LABELS "wasm;backend;conformance;structural"
    )

    # The same producer feeds real wasmtime when present. It is always
    # registered: absence is a visible SKIP (77), while a usable runtime that
    # rejects/misexecutes the artifact is a hard conformance failure.
    add_test(NAME ahfl.wasm.e1_execution_differential
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_e1_conformance.py"
                --probe $<TARGET_FILE:ahfl_core_wasm_e1_probe>
                --source
                    "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e1_identity_agent.ahfl"
                --explicit-wasmtime "${AHFL_WASMTIME}"
                --detected-wasmtime "${_ahfl_detected_wasmtime}"
    )
    set_tests_properties(ahfl.wasm.e1_execution_differential PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "real-wasmtime execution differential passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution"
    )
endif()

add_test(NAME ahfl.runtime.native_grpc_gate
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-native-grpc-gate.py"
)

add_test(NAME ahfl.runtime.transport_gate_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/transport_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-native-grpc-gate.py"
)

add_test(NAME ahfl.docs.ir_sync_gate
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-ir-doc-sync.py"
)

add_test(NAME ahfl.docs.rfc_check
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-rfc.py"
)

add_test(NAME ahfl.docs.rfc_check_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/rfc_check_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-rfc.py"
)

add_test(NAME ahfl.product.scope_freeze
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-product-scope-freeze.py"
)

add_test(NAME ahfl.product.scope_freeze_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/product_scope_freeze_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-product-scope-freeze.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.beta_gate_contract
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-beta-gate.py"
)

add_test(NAME ahfl.product.beta_gate_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/beta_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-beta-gate.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.controlled_pilot_gate_contract
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/check-controlled-pilot-gate.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.controlled_pilot_gate_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/controlled_pilot_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-controlled-pilot-gate.py"
)

add_test(NAME ahfl.product.production_confidence_gate_contract
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/check-production-confidence-gate.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.production_confidence_gate_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/production_confidence_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-production-confidence-gate.py"
)

add_test(NAME ahfl.product.production_confidence_ci_only_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/production_confidence_ci_only_smoke.py"
            "${PROJECT_SOURCE_DIR}/tests/scripts/reference_workflow_long_soak.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.reference_workflow.long_soak_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/reference_workflow_long_soak.py"
            --worker $<TARGET_FILE:ahfl_reference_workflow_recovery_worker>
            --repo-root "${PROJECT_SOURCE_DIR}"
            --work-dir "${CMAKE_BINARY_DIR}/runtime/reference-workflow-long-soak-smoke"
            --minimum-seconds 2
            --minimum-iterations 3
            --contract-kind smoke
            --evidence-path
            "${PROJECT_SOURCE_DIR}/build/release-evidence/production-confidence/long-soak-smoke.json"
)

add_test(NAME ahfl.product.runtime_evidence_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/runtime_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-runtime-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.beta_evidence_bundle_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/beta_evidence_bundle_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-evidence-bundle.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.beta_evidence_bundle_ready
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-evidence-bundle.py"
            --repo-root "${PROJECT_SOURCE_DIR}"
            --build-dir "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.formatter_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/formatter_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-formatter-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.stdlib_container_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/stdlib_container_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-stdlib-container-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.install_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/install_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-install-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.readme_capabilities
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-readme-capabilities.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.readme_capabilities_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/readme_capabilities_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-readme-capabilities.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.security.execution_demo_secret
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-execution-demo-secret.py"
)

add_test(NAME ahfl.security.execution_demo_secret_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/execution_demo_secret_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-execution-demo-secret.py"
            "${PROJECT_SOURCE_DIR}"
)

function(ahfl_label_tests)
    set(multi_value_args LABELS TESTS)
    cmake_parse_arguments(AHFL "" "" "${multi_value_args}" ${ARGN})

    if(NOT AHFL_LABELS)
        message(FATAL_ERROR "ahfl_label_tests requires LABELS")
    endif()

    if(NOT AHFL_TESTS)
        message(FATAL_ERROR "ahfl_label_tests requires TESTS")
    endif()

    foreach(test_name IN LISTS AHFL_TESTS)
        set_property(TEST "${test_name}" APPEND PROPERTY LABELS ${AHFL_LABELS})
    endforeach()
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/TestTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SingleFileCliTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BackendEffectExtraTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/InfraTargetTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/PassTraceTests.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/fuzz/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/bench/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/mutation/CMakeLists.txt")

include("${CMAKE_CURRENT_LIST_DIR}/cmake/LabelTests.cmake")

get_property(ahfl_all_tests DIRECTORY PROPERTY TESTS)
foreach(test_name IN LISTS ahfl_all_tests)
    set_property(TEST "${test_name}" PROPERTY WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()

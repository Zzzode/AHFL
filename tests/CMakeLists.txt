set(AHFL_EXAMPLES_DIR "${PROJECT_SOURCE_DIR}/examples")
set(AHFL_TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
set(AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE
    "${AHFL_TESTS_DIR}/integration/package_golden/ok_expr_temporal/ir/expr_temporal.ahfl"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME ahfl.architecture.boundaries
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-architecture.py"
)

# RFC 0027 P8 (KR6.13-G): negative compile-test for the IR node SSOT
# exhaustiveness gate. Syntax-only-compiles a fixture that rebuilds
# ahfl::ir::ExprNode from the shared .def list, injects one dummy
# alternative, and visits with one handler per real alternative. The
# injected build MUST fail with a diagnostic naming the unhandled type;
# removing the dummy alternative MUST compile.
add_test(NAME ahfl.ir.ssot_compile_fail
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/ir_ssot_compile_fail.py"
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

    # KR6.7 (RFC 0026 P7): the bespoke per-probe embedded-host lane was retired
    # in favor of ONE manifest-driven Node embedded host. The E2 OK result-frame
    # ownership (host-allocated OK frame decoded then released via dealloc),
    # ERROR/PENDING/unknown normalization and pending-latch contracts are now
    # produced from the e2 conformance case by
    # ahfl.conformance.wasm_node_differential (node_embedded_host.mjs ABI matrix);
    # the structural producer probe below still guards the module bytes.

    # RFC 0026 P6-1 (KR6.6): real-frontend arithmetic conditional-goto scalar
    # computation. The Node embedded host drives the stable step()/run ABI and
    # asserts the reached state ids and transition_count match native, including
    # the i64.div_s divide-by-zero trap. Real Node-engine evidence, visibly NOT
    # wasmtime evidence; SKIPs (77) when node is unavailable.
    add_test(NAME ahfl.wasm.p6_same_frontend_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_scalar_cond.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_scalar_cond.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_same_frontend_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "native_status=completed entered_ids=3,1,0 final_state_id=0 transition_count=2"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_scalar_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_scalar_cond.ahfl"
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_neg_compare.ahfl"
    )
    set_tests_properties(ahfl.wasm.p6_scalar_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "scalar Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 FB-1 (CORE-FNBODY-DESIGN): a real-frontend PURE direct call
    # (non-generic fn, a monomorphized generic fn, and an fn calling another
    # fn) reaches WASM as outlined functions invoked by a plain `call` with
    # env=0. The Node host compiles the module and drives step(); the scalar
    # return of the called fns routes the computed goto, so a wrong argument,
    # result word, or call target is visible in the state-id path.
    add_test(NAME ahfl.wasm.fb1_direct_call_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_fb1_direct_call_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/fb1_direct_call.ahfl"
    )
    set_tests_properties(ahfl.wasm.fb1_direct_call_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "FB-1 direct-call Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-1 (KR6.6): nested control flow. A fully-diverging if nested
    # inside a void/fall-through branch used to be typed i32 from divergence
    # alone, producing a module WebAssembly.compile rejects. These fixtures
    # cover nested/taken/fall-through/else-less/depth-3 shapes; the Node host
    # compiles each module and drives step() against the native state-id path.
    add_test(NAME ahfl.wasm.p6_nested_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_nested_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_nested_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "nested-control-flow Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-2 (KR6.6): structured control flow. Every computed handler now
    # compiles to its own `() -> i32` wasm FUNCTION whose body is a
    # `block (result i32)` wrapping the lowered region; step() dispatches by
    # CALL. The fixtures drive multi-way nested if/else cascades and else-less
    # fall-through, so the emitted `br` label depth tracks real nesting. The Node
    # host compiles each module (a wrong depth or an unbalanced if/else/end is a
    # hard WebAssembly.compile rejection) and drives step() against the native
    # state-id path.
    add_test(NAME ahfl.wasm.p6_structured_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_cascade.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_cascade.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_structured_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "entered_ids=5,2,0 final_state_id=0 transition_count=2 initial_state_id=5"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_structured_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_structured_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_structured_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "structured-control-flow Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-3 (KR6.6): match lowering. Arms compile to three nested wasm
    # blocks (S = match completion, B = the arm chain + fallback, C = one arm)
    # with the pattern test and guard negated into `br_if 0` (next arm); the
    # fallback is the code after the last arm and a Trap fallback is `unreachable`.
    # The structural probe pins the native state path for a statement if-let whose
    # arm diverges; the Node host compiles every fixture (a wrong `br` depth or an
    # unbalanced block/end is a hard WebAssembly.compile rejection), drives step()
    # against the native state-id path, and asserts a matched arm that divides by
    # zero raises a real WebAssembly.RuntimeError with no transition.
    add_test(NAME ahfl.wasm.p6_match_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_match_enum.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_match_enum.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_match_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "native_status=completed entered_ids=4,2,0 final_state_id=0 transition_count=2"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_match_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_match_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_match_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "match-lowering Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-4 (KR6.6): aggregate memory. A struct field is read from the
    # host-written input frame at its P4-D offset, a struct literal is
    # materialized through the scratch allocator, a nested field is projected
    # back, and a context field is stored at ITS P4-D offset. Observable output
    # still awaits the P6-7 frame decision, so the Node host writes the input
    # frame as RAW BYTES at the offsets the producer reports and reads the
    # transformed context sum back out of linear memory — real memory semantics,
    # no JSON in the loop. The producer pins the native state path; the host
    # compiles the module (a malformed module is a hard rejection) and asserts
    # both the state path and the read-back bytes.
    add_test(NAME ahfl.wasm.p6_aggregate_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_aggregate.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_aggregate.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_aggregate_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "native_status=completed entered_ids=3,1,0 final_state_id=0 transition_count=2"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_aggregate_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_aggregate_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_aggregate_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "aggregate-memory Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-6 (KR6.6): coercion physical effects. The KR6.5
    # `coercion_plans` arena gate is lifted; a CoreCoerceExpr lowers to a real
    # effect derived from the P4-D layout table. The producer emits a real
    # annotated-let widening fixture whose bounded side is the i32 scalar repr
    # and whose target is i64 (so the handler must carry i64.extend_i32_s), plus
    # a same-layout bounds relaxation (which must emit NO widening at all). The
    # producer pins the native state path; the Node host compiles the module (a
    # malformed body is a hard rejection), asserts the layout-derived opcode by
    # walking the CODE section, and drives step() against the native ids.
    add_test(NAME ahfl.wasm.p6_coerce_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_coerce.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_coerce.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_coerce_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "native_status=completed entered_ids=3,1,0 final_state_id=0 transition_count=2"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_coerce_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_coerce_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_coerce_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "coercion Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # RFC 0026 P6-5 (KR6.6): bounded collections. A bounded `List<Int>(4)` is an
    # i32 handle to an INLINE `(ptr,len)` header with an indirect element backing
    # store `stride` bytes apart; every fact (header offsets, stride, capacity,
    # backing size, element width) comes from the P4-D `CoreLayoutContainer`. The
    # fixture reads the length word and BOTH elements and branches on all three.
    # Observable `value_json` still awaits the P6-7 frame decision, so the Node
    # host writes the header + elements as RAW BYTES at the addresses the producer
    # reports, POISONS every slot outside the live length, drives step(), and
    # asserts the branch only a CORRECT header / stride / element-width / length
    # read can reach (a wrong stride, a wrong header offset, a mis-sized load, or
    # a wrong len word all route to the other branch or trap). The producer pins
    # the native state path and the P4-D facts. An UNBOUNDED collection has no
    # wasm32 backing store and still fails closed (P4-D `core.layout.UNBOUNDED`).
    add_test(NAME ahfl.wasm.p6_collection_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/p6_collection.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/p6_collection.wasm"
    )
    set_tests_properties(ahfl.wasm.p6_collection_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "native_status=completed entered_ids=3,1,0 final_state_id=0 transition_count=2 initial_state_id=3"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.p6_collection_node
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_p6_collection_node_host.py"
                $<TARGET_FILE:ahfl_core_wasm_p6_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm"
    )
    set_tests_properties(ahfl.wasm.p6_collection_node PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "bounded-collection Node embedded-engine execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;node"
    )

    # KR6.7 (RFC 0026 P7): the eligibility-promotion execution witness for
    # `float_output_e2e` is now produced manifest-driven by
    # ahfl.conformance.wasm_node_differential (the float_output_e2e committed
    # case): the generic Node embedded host runs the certified workflow-lane
    # module, asserts the step/current_state pre-effect trap, run2 borrowed-frame
    # passthrough and the native counters, and differentially compares the
    # observation against the evaluator. The bespoke eligibility host is retired.

    add_test(NAME ahfl.wasm.e2_same_frontend_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_e2_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e2_capability_agent.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/e2_same_frontend_probe.wasm"
    )
    set_tests_properties(ahfl.wasm.e2_same_frontend_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "final_state_id=0 transition_count=1 capability_calls=1 symbol_id=[0-9]+"
        LABELS "wasm;backend;conformance;structural"
    )

    # Real-wasmtime CLI can link a separate ahfl_cap provider for the no-frame
    # ERROR/PENDING paths. It cannot produce a conforming OK frame in the
    # target module's private memory, so that claim remains with an embedded
    # host and future release evidence.
    add_test(NAME ahfl.wasm.e2_wasmtime_error_pending
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_e2_wasmtime_conformance.py"
                --ahflc $<TARGET_FILE:ahflc>
                --source
                    "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e2_capability_agent.ahfl"
                --explicit-wasmtime "${AHFL_WASMTIME}"
                --detected-wasmtime "${_ahfl_detected_wasmtime}"
    )
    set_tests_properties(ahfl.wasm.e2_wasmtime_error_pending PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "real-wasmtime preload ERROR/PENDING execution passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;wasmtime"
    )

    # KR6.5 E3: the same checked frontend workflow feeds native
    # WorkflowRuntime and Core->layout->wasm. This always-on producer is
    # semantic/binary evidence, not wasm-engine execution by itself.
    add_test(NAME ahfl.wasm.e3_same_frontend_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_e3_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e3_identity_workflow.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/e3_same_frontend_probe.wasm"
    )
    set_tests_properties(ahfl.wasm.e3_same_frontend_probe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "schedule=first,second completed_nodes=2 transition_count=2 identity_output=1"
        LABELS "wasm;backend;conformance;structural"
    )

    add_test(NAME ahfl.wasm.e3_package_cli_binary_gate
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_e3_cli_gate.py"
                $<TARGET_FILE:ahflc>
                $<TARGET_FILE:ahfl_core_wasm_e3_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e3_identity_workflow.ahfl"
                "${PROJECT_SOURCE_DIR}/tests/integration/wasm_e3_workflow/ahfl.toml"
    )
    set_tests_properties(ahfl.wasm.e3_package_cli_binary_gate PROPERTIES
        PASS_REGULAR_EXPRESSION "E3 package CLI typed-entry byte identity passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;structural;cli"
    )

    # KR6.7 (RFC 0026 P7): persistent-instance workflow execution (both workflow
    # counters, idempotent run2 replay, borrowed-frame return, legacy run pointer
    # passthrough) is now driven manifest-style from the e3_identity_workflow
    # committed case by ahfl.conformance.wasm_node_differential. The bespoke
    # persistent-workflow Node host is retired.

    # Separate wasmtime CLI invocations cannot observe persistent globals, so
    # this optional test claims only legacy-run pointer passthrough. Missing
    # incidental tools SKIP; an explicitly configured broken tool hard-fails.
    add_test(NAME ahfl.wasm.e3_wasmtime_workflow_run
        COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tests/scripts/wasm_e3_wasmtime_conformance.py"
                --probe $<TARGET_FILE:ahfl_core_wasm_e3_probe>
                --source
                    "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e3_identity_workflow.ahfl"
                --explicit-wasmtime "${AHFL_WASMTIME}"
                --detected-wasmtime "${_ahfl_detected_wasmtime}"
    )
    set_tests_properties(ahfl.wasm.e3_wasmtime_workflow_run PROPERTIES
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "real-wasmtime workflow run passthrough passed"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;execution;wasmtime"
    )

    # RFC 0026 E4-B2-C: emit-only capability-workflow producer. Always-on
    # structural/binary evidence (not wasm-engine execution): it emits the
    # capability-workflow module through the real frontend->Core->emit path and
    # reports a non-empty import table.
    add_test(NAME ahfl.wasm.capability_workflow_same_frontend_probe
        COMMAND $<TARGET_FILE:ahfl_core_wasm_capability_workflow_probe>
                "${PROJECT_SOURCE_DIR}/tests/golden/wasm/e3_capability_workflow.ahfl"
                "${CMAKE_CURRENT_BINARY_DIR}/capability_workflow_probe.wasm"
    )
    set_tests_properties(ahfl.wasm.capability_workflow_same_frontend_probe PROPERTIES
        PASS_REGULAR_EXPRESSION "import_count=1 packaged_instances=2"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        LABELS "wasm;backend;conformance;structural"
    )

    # KR6.7 (RFC 0026 P7): the capability-workflow Node embedded-host matrix
    # (OK/PENDING/ERROR/unknown normalization, pending-latch re-entry negative,
    # pending-nonnull non-latching ERROR, the node-event record layout with
    # tag/ordinal/source_symbol, the corrupt-count defensive coordinate gate, and
    # the checked-alloc sequence) is now driven manifest-style from the
    # e3_capability_workflow committed case by
    # ahfl.conformance.wasm_node_differential's generic Node host. The 88-byte
    # contract-derived node-event KAT remains pinned byte-for-byte by
    # core_wasm_node_events.cpp. The bespoke capability-workflow Node host is
    # retired; the structural producer probe above still guards module bytes.
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

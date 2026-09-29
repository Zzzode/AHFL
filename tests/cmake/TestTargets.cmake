add_executable(ahfl_project_parse_tests
    integration/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_compiler_package_graph
        ahfl_compiler_syntax
)
target_include_directories(ahfl_project_parse_tests PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                            ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    integration/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_compiler_package_graph
        ahfl_compiler_semantics
)
target_include_directories(ahfl_project_resolve_tests PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                              ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    integration/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_compiler_package_graph
        ahfl_compiler_ir
        ahfl_runtime_evaluator
)
target_include_directories(ahfl_project_check_tests PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                            ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_compiler_ir_tests
    unit/compiler/ir/coercion_adjustment.cpp
    unit/compiler/ir/core_frame_layout.cpp
    unit/compiler/ir/core_layout.cpp
    unit/compiler/ir/core_lower.cpp
    unit/compiler/ir/core_lower_sysroot.cpp
    unit/compiler/ir/core_verify.cpp
    unit/compiler/ir/core_wire_migration.cpp
    unit/compiler/ir/core_wire_schema.cpp
    unit/compiler/ir/identity_visitor.cpp
    unit/compiler/ir/mangling.cpp
    unit/compiler/ir/matched_type_ref_bridge.cpp
    unit/compiler/ir/node_variant_coverage.cpp
    unit/compiler/ir/nominal_ref_bridge.cpp
    unit/compiler/ir/tower.cpp
    unit/compiler/ir/typed_hir_instance.cpp
    unit/compiler/ir/value_type_arena.cpp
)
target_link_libraries(ahfl_compiler_ir_tests
    PRIVATE
        ahfl_compiler_handoff
        doctest
)
target_include_directories(ahfl_compiler_ir_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(ahfl_compiler_ir_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_compiler_ir_tests)

add_executable(ahfl_compiler_ir_equal_tests
    unit/compiler/ir/ir_equal.cpp
)
target_link_libraries(ahfl_compiler_ir_equal_tests
    PRIVATE
        ahfl_compiler_ir
        doctest
)
target_include_directories(ahfl_compiler_ir_equal_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_compiler_ir_equal_tests)

add_executable(ahfl_compiler_ir_json_round_trip_tests
    unit/compiler/ir/ir_json_round_trip.cpp
)
target_link_libraries(ahfl_compiler_ir_json_round_trip_tests
    PRIVATE
        ahfl_compiler_ir
        doctest
)
target_include_directories(ahfl_compiler_ir_json_round_trip_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(ahfl_compiler_ir_json_round_trip_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_ir_json_round_trip_tests)


add_executable(ahfl_compiler_ir_core_json_round_trip_tests
    unit/compiler/ir/core_json_round_trip.cpp
)
target_link_libraries(ahfl_compiler_ir_core_json_round_trip_tests
    PRIVATE
        ahfl_compiler_handoff
        doctest
)
target_include_directories(ahfl_compiler_ir_core_json_round_trip_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(ahfl_compiler_ir_core_json_round_trip_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(ahfl_compiler_ir_core_json_round_trip_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_ir_core_json_round_trip_tests)


add_executable(ahfl_compiler_ir_opt_tests
    unit/compiler/ir/opt_ir.cpp
)
target_link_libraries(ahfl_compiler_ir_opt_tests
    PRIVATE
        ahfl_compiler_ir_opt
        doctest
)
ahfl_apply_project_warnings(ahfl_compiler_ir_opt_tests)

add_executable(ahfl_compiler_handoff_package_tests
    unit/compiler/handoff/package_model.cpp
)
target_link_libraries(ahfl_compiler_handoff_package_tests
    PRIVATE
        ahfl_compiler_handoff
)
ahfl_apply_project_warnings(ahfl_compiler_handoff_package_tests)

add_executable(ahfl_compiler_handoff_package_compat_tests
    unit/compiler/handoff/package_compat.cpp
)
target_link_libraries(ahfl_compiler_handoff_package_compat_tests
    PRIVATE
        ahfl_compiler_backend_pipeline_handoff
)
ahfl_apply_project_warnings(ahfl_compiler_handoff_package_compat_tests)

add_executable(ahfl_dry_run_tests
    unit/pipeline/execution/dry_run/runner.cpp
)
target_link_libraries(ahfl_dry_run_tests
    PRIVATE
        ahfl_pipeline_execution
)
ahfl_apply_project_warnings(ahfl_dry_run_tests)

add_executable(ahfl_runtime_evaluator_tests
    unit/runtime/evaluator/evaluator.cpp
)
target_link_libraries(ahfl_runtime_evaluator_tests
    PRIVATE
        ahfl_runtime_evaluator
)
ahfl_apply_project_warnings(ahfl_runtime_evaluator_tests)

add_executable(ahfl_executor_tests
    unit/runtime/evaluator/executor.cpp
)
target_link_libraries(ahfl_executor_tests
    PRIVATE
        ahfl_runtime_evaluator
)
ahfl_apply_project_warnings(ahfl_executor_tests)

add_executable(ahfl_agent_runtime_tests
    unit/runtime/engine/agent_runtime.cpp
)
target_link_libraries(ahfl_agent_runtime_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_agent_runtime_tests)

add_executable(ahfl_workflow_runtime_tests
    unit/runtime/engine/workflow_runtime.cpp
)
target_link_libraries(ahfl_workflow_runtime_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_workflow_runtime_tests)

add_executable(ahfl_execution_event_tests
    unit/runtime/engine/execution_event.cpp
)
target_link_libraries(ahfl_execution_event_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_execution_event_tests)

add_executable(ahfl_execution_report_tests
    unit/runtime/engine/execution_report.cpp
)
target_link_libraries(ahfl_execution_report_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_execution_report_tests)

add_executable(ahfl_execution_metadata_tests
    unit/runtime/engine/execution_metadata.cpp
)
target_link_libraries(ahfl_execution_metadata_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_execution_metadata_tests)

add_executable(ahfl_execution_renderer_tests
    unit/runtime/engine/execution_renderer.cpp
)
target_link_libraries(ahfl_execution_renderer_tests
    PRIVATE
        ahfl_runtime_engine
        ahfl_base_json
)
target_include_directories(ahfl_execution_renderer_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_execution_renderer_tests)

add_executable(ahfl_execution_projection_tests
    unit/runtime/engine/execution_projection.cpp
)
target_link_libraries(ahfl_execution_projection_tests
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_execution_projection_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_execution_projection_tests)

add_executable(ahfl_execution_otel_tests
    unit/runtime/engine/execution_otel.cpp
)
target_link_libraries(ahfl_execution_otel_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_execution_otel_tests)

add_executable(ahfl_workflow_recovery_tests
    unit/runtime/engine/workflow_recovery.cpp
)
target_link_libraries(ahfl_workflow_recovery_tests
    PRIVATE
        ahfl_runtime_engine
        doctest
)
target_include_directories(ahfl_workflow_recovery_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_workflow_recovery_tests)

add_executable(ahfl_capability_bridge_tests
    unit/runtime/engine/capability_bridge.cpp
)
target_link_libraries(ahfl_capability_bridge_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_capability_bridge_tests)

add_executable(ahfl_host_abi_tests
    unit/runtime/engine/ahfl_host_abi.cpp
)
target_link_libraries(ahfl_host_abi_tests
    PRIVATE
        ahfl_base_public
)
ahfl_apply_project_warnings(ahfl_host_abi_tests)

add_executable(ahfl_native_host_binding_tests
    unit/runtime/engine/native_host_binding.cpp
)
target_link_libraries(ahfl_native_host_binding_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_native_host_binding_tests)

add_executable(ahfl_native_wasm_differential_tests
    unit/runtime/engine/native_wasm_differential.cpp
)
target_link_libraries(ahfl_native_wasm_differential_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_native_wasm_differential_tests)

add_executable(ahfl_core_wire_codec_tests
    unit/runtime/engine/core_wire_codec.cpp
)
target_link_libraries(ahfl_core_wire_codec_tests
    PRIVATE
        ahfl_runtime_engine
        ahfl_base_json
)
ahfl_apply_project_warnings(ahfl_core_wire_codec_tests)

add_executable(ahfl_core_wasm_resume_record_tests
    unit/runtime/engine/core_wasm_resume_record.cpp
)
target_link_libraries(ahfl_core_wasm_resume_record_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_resume_record_tests)

if(AHFL_ENABLE_BACKEND_WASM)
    add_executable(ahfl_core_wasm_schema_module_tests
        unit/runtime/engine/core_wasm_schema_module.cpp
    )
    target_link_libraries(ahfl_core_wasm_schema_module_tests
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_core_wasm_schema_module_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_schema_module_tests)

    add_executable(ahfl_core_wasm_frame_module_tests
        unit/runtime/engine/core_wasm_frame_module.cpp
    )
    target_link_libraries(ahfl_core_wasm_frame_module_tests
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_runtime_engine
            ahfl_base_support
    )
    target_include_directories(ahfl_core_wasm_frame_module_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_frame_module_tests)
endif()

add_executable(ahfl_core_wasm_node_events_tests
    unit/runtime/engine/core_wasm_node_events.cpp
)
target_link_libraries(ahfl_core_wasm_node_events_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_node_events_tests)

add_executable(ahfl_core_wire_canonical_size_tests
    unit/runtime/engine/core_wire_canonical_size.cpp
)
target_link_libraries(ahfl_core_wire_canonical_size_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wire_canonical_size_tests)

add_executable(ahfl_core_wasm_resume_controller_tests
    unit/runtime/engine/core_wasm_resume_controller.cpp
)
target_link_libraries(ahfl_core_wasm_resume_controller_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_resume_controller_tests)

add_executable(ahfl_core_wasm_resume_capacity_tests
    unit/runtime/engine/core_wasm_resume_capacity.cpp
)
target_link_libraries(ahfl_core_wasm_resume_capacity_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_resume_capacity_tests)

add_executable(ahfl_core_wasm_resume_host_tests
    unit/runtime/engine/core_wasm_resume_host.cpp
)
target_link_libraries(ahfl_core_wasm_resume_host_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_resume_host_tests)

add_executable(ahfl_host_event_envelope_tests
    unit/runtime/engine/host_event_envelope.cpp
)
target_link_libraries(ahfl_host_event_envelope_tests
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_host_event_envelope_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_host_event_envelope_tests)

add_executable(ahfl_core_wasm_resume_host_codes_tests
    unit/runtime/engine/core_wasm_resume_host_codes.cpp
)
target_link_libraries(ahfl_core_wasm_resume_host_codes_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_core_wasm_resume_host_codes_tests)

add_executable(ahfl_payload_store_codec_tests
    unit/runtime/engine/payload_store_codec.cpp
)
target_link_libraries(ahfl_payload_store_codec_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_payload_store_codec_tests)

add_executable(ahfl_core_wasm_idempotency_token_tests
    unit/runtime/engine/core_wasm_idempotency_token.cpp
)
target_link_libraries(ahfl_core_wasm_idempotency_token_tests
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_core_wasm_idempotency_token_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_core_wasm_idempotency_token_tests)

add_executable(ahfl_durable_effect_intent_tests
    unit/runtime/engine/durable_effect_intent.cpp
)
target_link_libraries(ahfl_durable_effect_intent_tests
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_durable_effect_intent_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_durable_effect_intent_tests)

add_executable(ahfl_durable_effect_authority_tests
    unit/runtime/engine/durable_effect_authority.cpp
)
target_link_libraries(ahfl_durable_effect_authority_tests
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_durable_effect_authority_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_durable_effect_authority_tests)

# KR6.7 (RFC 0026 P7): engine-independent conformance case manifest schema
# validator. Pure test infrastructure: links only the JSON DOM + diagnostics,
# never an execution engine.
add_executable(ahfl_conformance_case_tests
    unit/runtime/conformance/conformance_case_test.cpp
)
target_link_libraries(ahfl_conformance_case_tests
    PRIVATE
        ahfl_base_json
)
target_include_directories(ahfl_conformance_case_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
target_compile_definitions(ahfl_conformance_case_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_conformance_case_tests)

# KR6.7 (RFC 0026 P7): generic in-process evaluator conformance runner. Links
# the actual tree-walking engine (ahfl_runtime_engine, which pulls the
# compiler pipeline) and the engine-independent manifest parser, and drives
# both over the checked-in case catalogue.
add_executable(ahfl_conformance_evaluator_runner
    conformance/evaluator_engine.cpp
    integration/conformance_evaluator_runner.cpp
)
target_link_libraries(ahfl_conformance_evaluator_runner
    PRIVATE
        ahfl_runtime_engine
        ahfl_base_json
)
target_include_directories(ahfl_conformance_evaluator_runner
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_conformance_evaluator_runner)

# KR6.7 (RFC 0026 P7): WASM eligibility classifier. Links the real compiler
# wasm backend (so it can actually run lower -> layout -> emit) plus the
# engine-independent manifest parser. Registered only when the executable
# wasm backend exists, because its whole point is to exercise that emit path.
if(AHFL_ENABLE_BACKEND_WASM)
    add_executable(ahfl_conformance_wasm_eligibility_tests
        unit/runtime/conformance/wasm_eligibility_test.cpp
        conformance/wasm_eligibility.cpp
    )
    target_link_libraries(ahfl_conformance_wasm_eligibility_tests
        PRIVATE
            ahfl_compiler_backend_wasm
    )
    target_include_directories(ahfl_conformance_wasm_eligibility_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_definitions(ahfl_conformance_wasm_eligibility_tests
        PRIVATE
            AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    ahfl_apply_project_warnings(ahfl_conformance_wasm_eligibility_tests)
endif()

# KR6.7 (RFC 0026 P7): manifest-driven Node embedded-engine differential
# runner. Produces the Core-Wasm module + machine-readable descriptor from each
# conformance case, drives the generic Node embedded host, and compares the
# evaluator and Node observations on the three differential dimensions. Links
# the real evaluator engine and the wasm backend (the producer emits real
# bytes); only registered when the executable wasm backend exists.
if(AHFL_ENABLE_BACKEND_WASM)
    add_executable(ahfl_conformance_wasm_node_runner
        conformance/evaluator_engine.cpp
        conformance/wasm_engine.cpp
        integration/conformance_wasm_node_runner.cpp
    )
    target_link_libraries(ahfl_conformance_wasm_node_runner
        PRIVATE
            ahfl_runtime_engine
            ahfl_compiler_backend_wasm
            ahfl_base_json
    )
    target_include_directories(ahfl_conformance_wasm_node_runner
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests
    )
    ahfl_apply_project_warnings(ahfl_conformance_wasm_node_runner)
endif()

add_executable(ahfl_payload_store_tests
    unit/runtime/engine/payload_store.cpp
)
target_link_libraries(ahfl_payload_store_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_payload_store_tests)

add_executable(ahfl_runtime_provider_llm_tests
    unit/runtime/providers/llm/llm_provider.cpp
)
target_link_libraries(ahfl_runtime_provider_llm_tests
    PRIVATE
        ahfl_runtime_provider_llm
)
ahfl_apply_project_warnings(ahfl_runtime_provider_llm_tests)

add_executable(ahfl_reference_workflow_recovery_worker
    integration/reference_workflow_recovery_worker.cpp
)
target_link_libraries(ahfl_reference_workflow_recovery_worker
    PRIVATE
        ahfl_compiler_package_graph
        ahfl_compiler_ir
        ahfl_runtime_provider_llm
)
target_include_directories(ahfl_reference_workflow_recovery_worker
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_reference_workflow_recovery_worker)

add_executable(ahfl_payload_store_worker
    integration/payload_store_worker.cpp
)
target_link_libraries(ahfl_payload_store_worker
    PRIVATE
        ahfl_runtime_engine
)
target_include_directories(ahfl_payload_store_worker
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_payload_store_worker)

add_executable(ahfl_durable_resume_capstone
    integration/durable_resume_capstone.cpp
)
target_link_libraries(ahfl_durable_resume_capstone
    PRIVATE
        ahfl_compiler_package_graph
        ahfl_compiler_ir
        ahfl_runtime_provider_llm
)
target_include_directories(ahfl_durable_resume_capstone
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_durable_resume_capstone)

add_executable(ahfl_value_json_tests
    unit/runtime/value/value_json.cpp
)
target_link_libraries(ahfl_value_json_tests
    PRIVATE
        ahfl_runtime_value
)
target_include_directories(ahfl_value_json_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_value_json_tests)

# RFC P7 runtime additions: Set / Map / UUID / Timestamp evaluation tests.
add_executable(ahfl_runtime_evaluator_p7_tests
    unit/runtime/evaluator/set_map_uuid_timestamp.cpp
)
target_link_libraries(ahfl_runtime_evaluator_p7_tests
    PRIVATE
        ahfl_runtime_evaluator
)
target_include_directories(ahfl_runtime_evaluator_p7_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(ahfl_runtime_evaluator_p7_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_runtime_evaluator_p7_tests)

# P2d.S5: evaluator end-to-end generics dispatch through mangled instance names.
add_executable(ahfl_runtime_evaluator_generics_tests
    unit/runtime/evaluator/evaluator_generics.cpp
)
target_link_libraries(ahfl_runtime_evaluator_generics_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_runtime_evaluator_generics_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(ahfl_runtime_evaluator_generics_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_runtime_evaluator_generics_tests)

add_executable(ahfl_counterexample_parse_tests
    unit/verification/formal/counterexample_parse.cpp
)
target_link_libraries(ahfl_counterexample_parse_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_counterexample_parse_tests)

add_executable(ahfl_smt_encode_tests
    unit/verification/formal/smt_encode.cpp
)
target_link_libraries(ahfl_smt_encode_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_smt_encode_tests)

add_executable(ahfl_subset_eligibility_tests
    unit/verification/formal/subset_eligibility.cpp
)
target_link_libraries(ahfl_subset_eligibility_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_subset_eligibility_tests)

add_executable(ahfl_smt_emit_tests
    unit/verification/formal/smt_emit.cpp
)
target_link_libraries(ahfl_smt_emit_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_smt_emit_tests)

add_executable(ahfl_smt_solver_tests
    unit/verification/formal/smt_solver.cpp
)
target_link_libraries(ahfl_smt_solver_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_smt_solver_tests)

add_executable(ahfl_smt_bmc_tests
    unit/verification/formal/smt_bmc.cpp
)
target_link_libraries(ahfl_smt_bmc_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_smt_bmc_tests)

add_executable(ahfl_http_transport_tests
    unit/runtime/engine/http_transport.cpp
)
target_link_libraries(ahfl_http_transport_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_http_transport_tests)

add_executable(ahfl_grpc_transport_tests
    unit/runtime/engine/grpc_transport.cpp
)
target_link_libraries(ahfl_grpc_transport_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_grpc_transport_tests)

add_executable(ahfl_wire_value_tests
    unit/runtime/engine/wire_value.cpp
)
target_link_libraries(ahfl_wire_value_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_wire_value_tests)

add_executable(ahfl_base_json_value_tests
    unit/base/json/json_value.cpp
)
target_link_libraries(ahfl_base_json_value_tests
    PRIVATE
        ahfl_base_json
)
ahfl_apply_project_warnings(ahfl_base_json_value_tests)

add_executable(ahfl_base_toml_tests
    unit/base/toml/toml.cpp
)
target_link_libraries(ahfl_base_toml_tests
    PRIVATE
        ahfl_base_toml
        doctest
)
ahfl_apply_project_warnings(ahfl_base_toml_tests)

add_executable(ahfl_base_query_tests
    unit/base/query/query_engine.cpp
)
target_link_libraries(ahfl_base_query_tests
    PRIVATE
        ahfl_base_query
        doctest
)
ahfl_apply_project_warnings(ahfl_base_query_tests)

# RFC 0027 P2 (KR6.11-S3): parse(file) derived query + the query-vs-direct
# equivalence guard over the golden corpus.
add_executable(ahfl_compiler_query_tests
    unit/compiler/query/parse_query_equiv.cpp
)
target_link_libraries(ahfl_compiler_query_tests
    PRIVATE
        ahfl_compiler_query
        doctest
)
target_compile_definitions(ahfl_compiler_query_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_query_tests)

# RFC 0027 P2 (KR6.11): hir(file) derived query + the query-vs-direct
# equivalence guard over the golden corpus. hir is a derived view over the
# typecheck memo (AHFL has no standalone HIR stage); the guard independently
# re-derives the direct TypedProgram and exercises the O(1) hir_expr accessor
# LSP will read. ahfl_compiler_query publicly links ahfl_compiler_semantics,
# which supplies serialize_typed_program_json and the TypedProgram store.
add_executable(ahfl_compiler_hir_query_tests
    unit/compiler/query/hir_query_equiv.cpp
)
target_link_libraries(ahfl_compiler_hir_query_tests
    PRIVATE
        ahfl_compiler_query
        doctest
)
target_compile_definitions(ahfl_compiler_hir_query_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_hir_query_tests)

# RFC 0027 P3 (KR6.11-S4): resolve / typecheck / type_of derived queries + the
# query-vs-direct equivalence guard over the golden corpus, including the IR JSON
# the CLI's golden fleet feeds from. ahfl_compiler_query publicly links
# ahfl_compiler_semantics (the result types), and ahfl_compiler_ir supplies
# lower_program_ir / print_program_ir_json for the byte-comparison half.
# ahfl_compiler_package_graph supplies the workspace→project-input path the
# project equivalence case drives the CLI's package arrival through.
add_executable(ahfl_compiler_frontend_queries_tests
    unit/compiler/query/frontend_queries_equiv.cpp
)
target_link_libraries(ahfl_compiler_frontend_queries_tests
    PRIVATE
        ahfl_compiler_query
        ahfl_compiler_ir
        ahfl_compiler_package_graph
        ahfl_base_support
        doctest
)
target_compile_definitions(ahfl_compiler_frontend_queries_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_include_directories(ahfl_compiler_frontend_queries_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
)
ahfl_apply_project_warnings(ahfl_compiler_frontend_queries_tests)

# RFC 0027 P2 (KR6.11-S5): the differential property guard — seeded random edit
# sequences must leave the incremental engine byte-identical to a cold-cache
# recomputation, at every step, on every projection (parse snapshot, the
# independently re-derived direct projection, and the resolve/typecheck/lower
# artifacts the engine-held AST drives). ahfl_compiler_ir supplies
# resolve/typecheck/lower for the downstream half of the property;
# ahfl_base_support supplies serialize_diagnostic_report_json.
add_executable(ahfl_compiler_query_edit_property_tests
    unit/compiler/query/incremental_equiv_property.cpp
)
target_link_libraries(ahfl_compiler_query_edit_property_tests
    PRIVATE
        ahfl_compiler_query
        ahfl_compiler_ir
        ahfl_base_support
        doctest
)
target_compile_definitions(ahfl_compiler_query_edit_property_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_query_edit_property_tests)

add_executable(ahfl_compiler_manifest_tests
    unit/compiler/manifest/manifest.cpp
)
target_link_libraries(ahfl_compiler_manifest_tests
    PRIVATE
        ahfl_compiler_manifest
        doctest
)
target_compile_definitions(ahfl_compiler_manifest_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_compiler_manifest_tests)

add_executable(ahfl_compiler_package_graph_tests
    unit/compiler/package_graph/package_graph.cpp
)
target_link_libraries(ahfl_compiler_package_graph_tests
    PRIVATE
        ahfl_compiler_package_graph
        doctest
)
ahfl_apply_project_warnings(ahfl_compiler_package_graph_tests)

add_executable(ahfl_base_diagnostic_serialization_tests
    unit/base/support/diagnostic_serialization.cpp
)
target_link_libraries(ahfl_base_diagnostic_serialization_tests
    PRIVATE
        ahfl_base_support
        ahfl_base_json
)
ahfl_apply_project_warnings(ahfl_base_diagnostic_serialization_tests)

add_executable(ahfl_base_trait_impl_diagnostics_tests
    unit/base/support/trait_impl_diagnostics.cpp
)
target_link_libraries(ahfl_base_trait_impl_diagnostics_tests
    PRIVATE
        ahfl_base_support
)
ahfl_apply_project_warnings(ahfl_base_trait_impl_diagnostics_tests)
add_executable(ahfl_base_diagnostics_code_smoke_tests
    unit/base/support/diagnostics_code_smoke.cpp
)
target_link_libraries(ahfl_base_diagnostics_code_smoke_tests
    PRIVATE
        ahfl_base_support
)
ahfl_apply_project_warnings(ahfl_base_diagnostics_code_smoke_tests)

add_executable(ahfl_runtime_provider_secret_provider_tests
    unit/runtime/providers/secret/secret_provider.cpp
)
target_link_libraries(ahfl_runtime_provider_secret_provider_tests
    PRIVATE
        ahfl_runtime_provider_secret
)
ahfl_apply_project_warnings(ahfl_runtime_provider_secret_provider_tests)

add_executable(ahfl_vault_rotation_tests
    unit/runtime/providers/secret/vault_rotation.cpp
)
target_link_libraries(ahfl_vault_rotation_tests
    PRIVATE
        ahfl_runtime_provider_secret
)
ahfl_apply_project_warnings(ahfl_vault_rotation_tests)

add_executable(ahfl_pass_manager_tests
    unit/compiler/passes/pass_manager.cpp
)
target_link_libraries(ahfl_pass_manager_tests
    PRIVATE
        ahfl_compiler_passes
        doctest
)
ahfl_apply_project_warnings(ahfl_pass_manager_tests)

add_executable(ahfl_transform_passes_tests
    unit/compiler/passes/transform_passes.cpp
)
target_link_libraries(ahfl_transform_passes_tests
    PRIVATE
        ahfl_compiler_passes
        doctest
)
ahfl_apply_project_warnings(ahfl_transform_passes_tests)

add_executable(ahfl_semantics_type_relations_tests
    unit/compiler/semantics/type_relations.cpp
)
target_link_libraries(ahfl_semantics_type_relations_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_type_relations_tests)

add_executable(ahfl_semantics_type_resolver_tests
    unit/compiler/semantics/type_resolver.cpp
)
target_link_libraries(ahfl_semantics_type_resolver_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_type_resolver_tests)

add_executable(ahfl_semantics_typed_hir_tests
    unit/compiler/semantics/typed_hir.cpp
    unit/compiler/semantics/typed_hir_kind_coverage.cpp
)
target_link_libraries(ahfl_semantics_typed_hir_tests
    PRIVATE
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_typed_hir_tests)

add_executable(ahfl_semantics_effects_tests
    unit/compiler/semantics/effects.cpp
)
target_link_libraries(ahfl_semantics_effects_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_effects_tests)

add_executable(ahfl_semantics_pattern_usefulness_tests
    unit/compiler/semantics/pattern_usefulness.cpp
)
target_link_libraries(ahfl_semantics_pattern_usefulness_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_pattern_usefulness_tests)

add_executable(ahfl_semantics_diagnostic_matrix_tests
    unit/compiler/semantics/diagnostic_matrix.cpp
)
target_link_libraries(ahfl_semantics_diagnostic_matrix_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
target_compile_definitions(ahfl_semantics_diagnostic_matrix_tests
    PRIVATE
        AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
ahfl_apply_project_warnings(ahfl_semantics_diagnostic_matrix_tests)

add_executable(ahfl_semantics_type_mismatch_origin_tests
    unit/compiler/semantics/type_mismatch_origin.cpp
)
target_link_libraries(ahfl_semantics_type_mismatch_origin_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        doctest
)
target_include_directories(ahfl_semantics_type_mismatch_origin_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)
ahfl_apply_project_warnings(ahfl_semantics_type_mismatch_origin_tests)

add_executable(ahfl_semantics_stmt_diagnostics_tests
    unit/compiler/semantics/stmt_diagnostics.cpp
)
target_link_libraries(ahfl_semantics_stmt_diagnostics_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_semantics_stmt_diagnostics_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)
ahfl_apply_project_warnings(ahfl_semantics_stmt_diagnostics_tests)

add_executable(ahfl_semantics_try_operator_tests
    unit/compiler/semantics/try_operator.cpp
)
target_link_libraries(ahfl_semantics_try_operator_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_semantics_try_operator_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)
ahfl_apply_project_warnings(ahfl_semantics_try_operator_tests)

add_executable(ahfl_semantics_bounded_quantifier_tests
    unit/compiler/semantics/bounded_quantifier.cpp
)
target_link_libraries(ahfl_semantics_bounded_quantifier_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_semantics_bounded_quantifier_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)
ahfl_apply_project_warnings(ahfl_semantics_bounded_quantifier_tests)

add_executable(ahfl_semantics_const_sema_negatives_tests
    unit/compiler/semantics/const_sema_negatives.cpp
)
target_link_libraries(ahfl_semantics_const_sema_negatives_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
target_include_directories(ahfl_semantics_const_sema_negatives_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)
ahfl_apply_project_warnings(ahfl_semantics_const_sema_negatives_tests)

add_executable(ahfl_semantics_c4_capture_list_tests
    unit/compiler/semantics/c4_capture_list.cpp
)
target_link_libraries(ahfl_semantics_c4_capture_list_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_tooling_formatter
        doctest
)
target_include_directories(ahfl_semantics_c4_capture_list_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_semantics_c4_capture_list_tests)
add_test(NAME c4_capture_list COMMAND ahfl_semantics_c4_capture_list_tests)

add_executable(ahfl_semantics_b2_impl_body_parser_gaps_tests
    unit/compiler/semantics/b2_impl_body_parser_gaps.cpp
)
target_link_libraries(ahfl_semantics_b2_impl_body_parser_gaps_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_compiler_ir_opt
        ahfl_compiler_backends
        ahfl_runtime_evaluator
        ahfl_runtime_engine
        ahfl_tooling_formatter
        doctest
)
target_include_directories(ahfl_semantics_b2_impl_body_parser_gaps_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_semantics_b2_impl_body_parser_gaps_tests)
add_test(NAME b2_impl_body_parser_gaps COMMAND ahfl_semantics_b2_impl_body_parser_gaps_tests)

add_executable(ahfl_semantics_p2_s1_inference_tests
    unit/compiler/semantics/p2_s1_inference.cpp
)
target_link_libraries(ahfl_semantics_p2_s1_inference_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_compiler_ir_opt
        ahfl_compiler_backends
        ahfl_runtime_evaluator
        ahfl_runtime_engine
        ahfl_tooling_formatter
        doctest
)
target_include_directories(ahfl_semantics_p2_s1_inference_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_semantics_p2_s1_inference_tests)
add_test(NAME p2_s1_inference COMMAND ahfl_semantics_p2_s1_inference_tests)

add_executable(ahfl_semantics_d3_decreases_expr_tests
    unit/compiler/semantics/d3_decreases_expr.cpp
)
target_link_libraries(ahfl_semantics_d3_decreases_expr_tests
    PRIVATE
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_semantics_d3_decreases_expr_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${PROJECT_SOURCE_DIR}/src
)
ahfl_apply_project_warnings(ahfl_semantics_d3_decreases_expr_tests)
add_test(NAME d3_decreases_expr COMMAND ahfl_semantics_d3_decreases_expr_tests)

add_executable(ahfl_semantics_flow_condition_tests
    unit/compiler/semantics/flow_condition.cpp
)
target_link_libraries(ahfl_semantics_flow_condition_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_flow_condition_tests)

add_executable(ahfl_semantics_validate_plumbing_tests
    unit/compiler/semantics/validate_plumbing.cpp
)
target_link_libraries(ahfl_semantics_validate_plumbing_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_validate_plumbing_tests)

add_executable(ahfl_semantics_adt_match_tests
    unit/compiler/semantics/adt_match.cpp
)
target_link_libraries(ahfl_semantics_adt_match_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_adt_match_tests)

add_executable(ahfl_semantics_fn_generics_closures_tests
    unit/compiler/semantics/fn_generics_closures.cpp
)
target_link_libraries(ahfl_semantics_fn_generics_closures_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_fn_generics_closures_tests)

add_executable(ahfl_semantics_trait_impl_tests
    unit/compiler/semantics/trait_impl.cpp
)
target_link_libraries(ahfl_semantics_trait_impl_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_trait_impl_tests)

add_executable(ahfl_semantics_decreases_recognizer_tests
    unit/compiler/semantics/decreases_recognizer.cpp
)
target_link_libraries(ahfl_semantics_decreases_recognizer_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_decreases_recognizer_tests)

add_executable(ahfl_semantics_concurrency_tests
    unit/compiler/semantics/concurrency.cpp
)
target_link_libraries(ahfl_semantics_concurrency_tests
    PRIVATE
        ahfl_compiler_ir
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_concurrency_tests)

add_executable(ahfl_semantics_where_clause_info_tests
    unit/compiler/semantics/where_clause_info.cpp
)
target_link_libraries(ahfl_semantics_where_clause_info_tests
    PRIVATE
        ahfl_compiler_ir
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_where_clause_info_tests)

add_executable(ahfl_semantics_monomorphization_tests
    unit/compiler/semantics/monomorphization.cpp
)
target_link_libraries(ahfl_semantics_monomorphization_tests
    PRIVATE
        ahfl_compiler_semantics
        doctest
)
ahfl_apply_project_warnings(ahfl_semantics_monomorphization_tests)

# P4.S7b: assurance verification.obligations classifier + JSON schema tests.
add_executable(ahfl_assurance_obligations_tests
    unit/compiler/assurance/obligations.cpp
)
target_link_libraries(ahfl_assurance_obligations_tests
    PRIVATE
        ahfl_compiler_ir
        ahfl_compiler_assurance
        ahfl_compiler_backends
        ahfl_runtime_evaluator
        doctest
)
target_include_directories(ahfl_assurance_obligations_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_assurance_obligations_tests)

add_executable(ahfl_streaming_tests
    unit/runtime/providers/llm/streaming.cpp
)
target_link_libraries(ahfl_streaming_tests
    PRIVATE
        ahfl_runtime_provider_llm
)
ahfl_apply_project_warnings(ahfl_streaming_tests)

add_executable(ahfl_tooling_lsp_json_rpc_tests
    unit/tooling/lsp/json_rpc.cpp
)
target_link_libraries(ahfl_tooling_lsp_json_rpc_tests
    PRIVATE
        ahfl_tooling_lsp
)
ahfl_apply_project_warnings(ahfl_tooling_lsp_json_rpc_tests)

add_executable(ahfl_tooling_lsp_handler_tests
    unit/tooling/lsp/server_handlers.cpp
)
target_link_libraries(ahfl_tooling_lsp_handler_tests
    PRIVATE
        ahfl_tooling_lsp
)
ahfl_apply_project_warnings(ahfl_tooling_lsp_handler_tests)

# RFC 0027 P4 (KR6.12): deterministic edit-sequence equivalence at the pure
# LSP analysis-engine level — no JSON-RPC / display server needed.
add_executable(ahfl_tooling_lsp_analysis_edits_tests
    unit/tooling/lsp/analysis_engine_edits.cpp
)
target_link_libraries(ahfl_tooling_lsp_analysis_edits_tests
    PRIVATE
        ahfl_tooling_lsp
)
ahfl_apply_project_warnings(ahfl_tooling_lsp_analysis_edits_tests)

add_executable(ahfl_connection_pool_tests
    unit/runtime/engine/connection_pool.cpp
)
target_link_libraries(ahfl_connection_pool_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_connection_pool_tests)

add_executable(ahfl_error_recovery_tests
    unit/compiler/syntax/frontend/error_recovery.cpp
)
target_link_libraries(ahfl_error_recovery_tests
    PRIVATE
        ahfl_compiler_syntax_recovery
)
ahfl_apply_project_warnings(ahfl_error_recovery_tests)

# Wave-21 A-1: parser recursion-depth guard. Covers 4 nesting categories
# (paren/expr, type parameter, block/statement, pattern) with under-limit +
# over-limit cases, plus a mixed under-limit sanity (10 test functions ×
# ~2-3 assertions each = ~48 total assertions).
add_executable(ahfl_parser_stack_depth_tests
    unit/compiler/syntax/frontend/parser_stack_depth.cpp
)
target_link_libraries(ahfl_parser_stack_depth_tests
    PRIVATE
        ahfl_compiler_syntax
)
ahfl_apply_project_warnings(ahfl_parser_stack_depth_tests)

add_executable(ahfl_syntax_trait_impl_tests
    unit/compiler/syntax/trait_impl.cpp
)
target_link_libraries(ahfl_syntax_trait_impl_tests
    PRIVATE
        ahfl_compiler_syntax
        doctest
)
ahfl_apply_project_warnings(ahfl_syntax_trait_impl_tests)
# DecreasesClauseSyntax – standalone fragment tests (R-09: not in DeclKind).
# Split into small translation units so each acceptance dimension has an
# independently-failing ctest target.
add_executable(ahfl_decreases_structure_tests
    unit/compiler/syntax/frontend/decreases_structure.cpp
)
target_link_libraries(ahfl_decreases_structure_tests
    PRIVATE
        ahfl_compiler_syntax
)
ahfl_apply_project_warnings(ahfl_decreases_structure_tests)

add_executable(ahfl_decreases_printer_tests
    unit/compiler/syntax/frontend/decreases_printer.cpp
)
target_link_libraries(ahfl_decreases_printer_tests
    PRIVATE
        ahfl_compiler_syntax
)
ahfl_apply_project_warnings(ahfl_decreases_printer_tests)

add_executable(ahfl_decreases_desugar_tests
    unit/compiler/syntax/frontend/decreases_desugar.cpp
)
target_link_libraries(ahfl_decreases_desugar_tests
    PRIVATE
        ahfl_compiler_syntax
)
ahfl_apply_project_warnings(ahfl_decreases_desugar_tests)

add_executable(ahfl_decreases_symmetry_tests
    unit/compiler/syntax/frontend/decreases_symmetry.cpp
)
target_link_libraries(ahfl_decreases_symmetry_tests
    PRIVATE
        ahfl_compiler_syntax
        ahfl_tooling_formatter
)
ahfl_apply_project_warnings(ahfl_decreases_symmetry_tests)

# RFC 0001 enum struct variant syntax coverage: parser / AST / ast_printer /
# formatter roundtrip. Semantic, Typed HIR, IR, and runtime coverage lives in
# the dedicated semantics/runtime suites.
add_executable(ahfl_enum_struct_variant_tests
    unit/compiler/syntax/frontend/enum_struct_variant.cpp
)
target_link_libraries(ahfl_enum_struct_variant_tests
    PRIVATE
        ahfl_compiler_syntax
        ahfl_tooling_formatter
        doctest
)
ahfl_apply_project_warnings(ahfl_enum_struct_variant_tests)

# RFC 0011 if-let pattern syntax coverage: parser / AST / ast_printer /
# formatter roundtrip.
add_executable(ahfl_if_let_syntax_tests
    unit/compiler/syntax/frontend/if_let_syntax.cpp
)
target_link_libraries(ahfl_if_let_syntax_tests
    PRIVATE
        ahfl_compiler_syntax
        ahfl_tooling_formatter
        doctest
)
ahfl_apply_project_warnings(ahfl_if_let_syntax_tests)

# RFC 0013 P4: `List<T> where length <= N` capacity refinement sugar. Parser /
# AST coverage proving it lowers to the same NamedType::collection_capacity the
# nominal `(N)` form populates, plus the INVALID_CAPACITY_REFINEMENT negative.
add_executable(ahfl_capacity_refinement_sugar_tests
    unit/compiler/syntax/frontend/capacity_refinement_sugar.cpp
)
target_link_libraries(ahfl_capacity_refinement_sugar_tests
    PRIVATE
        ahfl_compiler_syntax
        doctest
)
ahfl_apply_project_warnings(ahfl_capacity_refinement_sugar_tests)

# RFC 0013 P6: `#![no_prelude]` inner attribute parser coverage.
add_executable(ahfl_inner_attribute_tests
    unit/compiler/syntax/frontend/inner_attribute.cpp
)
target_link_libraries(ahfl_inner_attribute_tests
    PRIVATE
        ahfl_compiler_syntax
        doctest
)
ahfl_apply_project_warnings(ahfl_inner_attribute_tests)

add_executable(ahfl_thread_pool_tests
    unit/base/support/thread_pool.cpp
)
target_link_libraries(ahfl_thread_pool_tests
    PRIVATE
        ahfl_base_support
)
ahfl_apply_project_warnings(ahfl_thread_pool_tests)

add_executable(ahfl_sha256_tests
    unit/base/support/sha256.cpp
)
target_link_libraries(ahfl_sha256_tests
    PRIVATE
        ahfl_base_support
        doctest
)
ahfl_apply_project_warnings(ahfl_sha256_tests)

add_executable(ahfl_atomic_file_tests
    unit/base/support/atomic_file.cpp
)
target_link_libraries(ahfl_atomic_file_tests
    PRIVATE
        ahfl_base_support
        doctest
)
ahfl_apply_project_warnings(ahfl_atomic_file_tests)

add_executable(ahfl_version_tests
    unit/base/support/version.cpp
)
target_link_libraries(ahfl_version_tests
    PRIVATE
        ahfl_base_support
)
ahfl_apply_project_warnings(ahfl_version_tests)

add_executable(ahfl_bmc_tests
    unit/verification/formal/bmc.cpp
)
target_link_libraries(ahfl_bmc_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_bmc_tests)

add_executable(ahfl_model_checker_tests
    unit/verification/formal/model_checker_backends.cpp
)
target_link_libraries(ahfl_model_checker_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_model_checker_tests)

add_executable(ahfl_verification_formal_integration_tests
    unit/verification/formal/integration_improvement.cpp
)
target_link_libraries(ahfl_verification_formal_integration_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_verification_formal_integration_tests)

add_executable(ahfl_bmc_depth_customization_tests
    unit/verification/formal/bmc_depth_customization.cpp
    ${PROJECT_SOURCE_DIR}/src/tooling/cli/option_table.cpp
)
target_link_libraries(ahfl_bmc_depth_customization_tests
    PRIVATE
        ahfl_verification_formal
        ahfl_cli_command_catalog
)
ahfl_apply_project_warnings(ahfl_bmc_depth_customization_tests)

add_executable(ahfl_parallel_scheduler_tests
    unit/runtime/engine/parallel_scheduler.cpp
)
target_link_libraries(ahfl_parallel_scheduler_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_parallel_scheduler_tests)

add_executable(ahfl_sandbox_tests
    unit/runtime/engine/sandbox.cpp
)
target_link_libraries(ahfl_sandbox_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_sandbox_tests)

add_executable(ahfl_distributed_tests
    unit/runtime/engine/distributed.cpp
)
target_link_libraries(ahfl_distributed_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_distributed_tests)

add_executable(ahfl_tooling_formatter_tests
    unit/tooling/formatter/formatter.cpp
)
target_link_libraries(ahfl_tooling_formatter_tests
    PRIVATE
        ahfl_tooling_formatter
)
ahfl_apply_project_warnings(ahfl_tooling_formatter_tests)

add_executable(ahfl_tooling_repl_tests
    unit/tooling/repl/repl.cpp
)
target_link_libraries(ahfl_tooling_repl_tests
    PRIVATE
        ahfl_tooling_repl
)
ahfl_apply_project_warnings(ahfl_tooling_repl_tests)

add_executable(ahfl_tooling_dap_tests
    unit/tooling/dap/dap_basic.cpp
)
target_link_libraries(ahfl_tooling_dap_tests
    PRIVATE
        ahfl_tooling_dap
)
target_include_directories(ahfl_tooling_dap_tests PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                         ${PROJECT_SOURCE_DIR}/tests)
ahfl_apply_project_warnings(ahfl_tooling_dap_tests)

add_executable(ahfl_tooling_telemetry_tests
    unit/tooling/telemetry/telemetry.cpp
)
target_link_libraries(ahfl_tooling_telemetry_tests
    PRIVATE
        ahfl_tooling_telemetry
)
ahfl_apply_project_warnings(ahfl_tooling_telemetry_tests)

add_executable(ahfl_tooling_profiling_tests
    unit/tooling/profiling/profiling.cpp
)
target_link_libraries(ahfl_tooling_profiling_tests
    PRIVATE
        ahfl_tooling_profiling
)
ahfl_apply_project_warnings(ahfl_tooling_profiling_tests)

add_executable(ahfl_tooling_abi_tests
    unit/tooling/abi/abi_compat.cpp
)
target_link_libraries(ahfl_tooling_abi_tests
    PRIVATE
        ahfl_tooling_abi
)
ahfl_apply_project_warnings(ahfl_tooling_abi_tests)

# RFC 0016 cache contract tests for the QueryEngine persistence layer
# (RFC 0027 P5 / KR6.12). The hand-rolled incremental subsystem they used to
# live beside is retired; only the deterministic persistent-cache contract
# remains, re-anchored as ahfl_tooling_cache.
add_executable(ahfl_tooling_cache_tests
    unit/tooling/cache/cache_core.cpp
)
target_link_libraries(ahfl_tooling_cache_tests
    PRIVATE
        ahfl_tooling_cache
)
ahfl_apply_project_warnings(ahfl_tooling_cache_tests)

if(AHFL_ENABLE_BACKEND_WASM)
    # RFC 0026 KR6.8 WH-0: the vendored wasm3 static library has no production
    # caller yet; this probe is its only consumer and proves the archive links
    # and executes inside the AHFL build. It deliberately links `wasm3` alone.
    add_executable(ahfl_wasm3_smoke_tests
        unit/runtime/wasm_host/wasm3_smoke.cpp
    )
    target_link_libraries(ahfl_wasm3_smoke_tests
        PRIVATE
            wasm3
    )
    ahfl_apply_project_warnings(ahfl_wasm3_smoke_tests)

    # RFC 0026 KR6.8 WH-1: the wasm3-backed CoreWasmResumeEngine port unit
    # tests. Links the engine under test + the runtime engine (A2 admission +
    # the shared resume_test_support fixture builders). src/ reaches the test
    # through the engine target's BUILD_INTERFACE; tests/ is added for the
    # unit/ fixture includes. Gated with the wasm3 archive it drives.
    add_executable(ahfl_wasm_host_engine_tests
        unit/runtime/wasm_host/wasm3_engine.cpp
    )
    target_link_libraries(ahfl_wasm_host_engine_tests
        PRIVATE
            ahfl_runtime_wasm_host
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_wasm_host_engine_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests
    )
    ahfl_apply_project_warnings(ahfl_wasm_host_engine_tests)

    add_executable(ahfl_core_wasm_codegen_tests
        unit/compiler/backends/core_wasm_codegen.cpp
    )
    target_link_libraries(ahfl_core_wasm_codegen_tests
        PRIVATE
            ahfl_compiler_backend_wasm
    )
    ahfl_apply_project_warnings(ahfl_core_wasm_codegen_tests)

    add_executable(ahfl_core_wasm_e1_probe
        integration/core_wasm_e1_probe.cpp
    )
    target_link_libraries(ahfl_core_wasm_e1_probe
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_core_wasm_e1_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_e1_probe)

    add_executable(ahfl_core_wasm_e2_probe
        integration/core_wasm_e2_probe.cpp
    )
    target_link_libraries(ahfl_core_wasm_e2_probe
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_core_wasm_e2_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_e2_probe)

    # RFC 0026 P6-1 (KR6.6): real-frontend scalar-computation computed-goto
    # producer. Links the same backend + runtime as the E1/E3 probes.
    add_executable(ahfl_core_wasm_p6_probe
        integration/core_wasm_p6_probe.cpp
    )
    target_link_libraries(ahfl_core_wasm_p6_probe
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_compiler_package_graph
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_core_wasm_p6_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_include_directories(ahfl_core_wasm_p6_probe PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    ahfl_apply_project_warnings(ahfl_core_wasm_p6_probe)

    add_executable(ahfl_core_wasm_e3_probe
        integration/core_wasm_e3_probe.cpp
    )
    target_link_libraries(ahfl_core_wasm_e3_probe
        PRIVATE
            ahfl_compiler_backend_wasm
            ahfl_runtime_engine
    )
    target_include_directories(ahfl_core_wasm_e3_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_e3_probe)

    # RFC 0026 E4-B2-C: emit-only capability-workflow producer. Per the Q-P1
    # ruling it links ONLY the compiler wasm backend (NOT ahfl_runtime_engine);
    # it never calls the A2 runtime module context.
    add_executable(ahfl_core_wasm_capability_workflow_probe
        integration/core_wasm_capability_workflow_probe.cpp
    )
    target_link_libraries(ahfl_core_wasm_capability_workflow_probe
        PRIVATE
            ahfl_compiler_backend_wasm
    )
    target_include_directories(ahfl_core_wasm_capability_workflow_probe
        PRIVATE ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(ahfl_core_wasm_capability_workflow_probe)

    # RFC 0026 E4-B2-D2a (F5): end-to-end durable-resume replay of the emitted
    # capability workflow over a REAL Node embedded Wasm engine. The binary links
    # only the runtime engine (+ base support for process.hpp/find_executable);
    # the emit-only capability-workflow probe is a ctest-level build dependency,
    # launched as a subprocess to produce the real artifact under replay.
    add_executable(ahfl_core_wasm_resume_node_e2e
        integration/core_wasm_node_resume_engine.cpp
        integration/core_wasm_resume_node_e2e.cpp
    )
    target_link_libraries(ahfl_core_wasm_resume_node_e2e
        PRIVATE
            ahfl_runtime_engine
            ahfl_base_support
    )
    target_include_directories(ahfl_core_wasm_resume_node_e2e
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests
    )
    ahfl_apply_project_warnings(ahfl_core_wasm_resume_node_e2e)
endif()

add_executable(ahfl_compiler_backends_registry_tests
    unit/compiler/backends/registry.cpp
)
target_link_libraries(ahfl_compiler_backends_registry_tests
    PRIVATE
        ahfl_compiler_backends
)
ahfl_apply_project_warnings(ahfl_compiler_backends_registry_tests)

add_executable(ahfl_cli_command_routing_tests
    unit/tooling/cli/command_routing.cpp
    ${PROJECT_SOURCE_DIR}/src/tooling/cli/option_table.cpp
)
target_link_libraries(ahfl_cli_command_routing_tests
    PRIVATE
        ahfl_cli_command_catalog
        ahfl_base_public
)
ahfl_apply_project_warnings(ahfl_cli_command_routing_tests)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_executable(ahfl_target_backends_tests
        unit/compiler/backends/target_backends.cpp
    )
    target_link_libraries(ahfl_target_backends_tests
        PRIVATE
            ahfl_compiler_backend_infra_k8s_crd
            ahfl_compiler_backend_infra_openapi_spec
            ahfl_compiler_backend_infra_terraform_gen
            ahfl_compiler_backend_infra_type_schema
    )
    ahfl_apply_project_warnings(ahfl_target_backends_tests)
endif()

add_executable(ahfl_tooling_package_tests
    unit/tooling/package/package.cpp
)
target_link_libraries(ahfl_tooling_package_tests
    PRIVATE
        ahfl_tooling_package
)
ahfl_apply_project_warnings(ahfl_tooling_package_tests)

add_executable(ahfl_property_lowering_tests
    unit/property/lowering_equiv.cpp
)
target_link_libraries(ahfl_property_lowering_tests
    PRIVATE
        ahfl_tooling_testing
        ahfl_compiler_handoff
        ahfl_compiler_ir
        ahfl_compiler_ir_opt
)
target_include_directories(ahfl_property_lowering_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_property_lowering_tests)

add_executable(ahfl_property_core_erasure_tests
    unit/property/core_erasure.cpp
)
target_link_libraries(ahfl_property_core_erasure_tests
    PRIVATE
        ahfl_compiler_handoff
        ahfl_compiler_ir
)
target_include_directories(ahfl_property_core_erasure_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_property_core_erasure_tests)

add_executable(ahfl_property_smv_tests
    unit/property/smv_syntax.cpp
)
target_link_libraries(ahfl_property_smv_tests
    PRIVATE
        ahfl_tooling_testing
)
ahfl_apply_project_warnings(ahfl_property_smv_tests)

# Test targets that directly include internal src/ headers need PRIVATE access
foreach(_tgt
    ahfl_dry_run_tests
    ahfl_compiler_handoff_package_compat_tests
    ahfl_compiler_backends_registry_tests
    ahfl_cli_command_routing_tests
    ahfl_runtime_evaluator_tests
    ahfl_executor_tests
    ahfl_agent_runtime_tests
    ahfl_workflow_runtime_tests
    ahfl_workflow_recovery_tests
    ahfl_capability_bridge_tests
    ahfl_native_host_binding_tests
    ahfl_native_wasm_differential_tests
    ahfl_core_wire_codec_tests
    ahfl_core_wasm_resume_record_tests
    ahfl_core_wasm_node_events_tests
    ahfl_core_wire_canonical_size_tests
    ahfl_core_wasm_resume_controller_tests
    ahfl_core_wasm_resume_host_codes_tests
    ahfl_core_wasm_resume_capacity_tests
    ahfl_core_wasm_resume_host_tests
    ahfl_payload_store_codec_tests
    ahfl_payload_store_tests
    ahfl_runtime_provider_llm_tests
    ahfl_reference_workflow_recovery_worker
    ahfl_payload_store_worker
    ahfl_durable_resume_capstone
    ahfl_value_json_tests
    ahfl_counterexample_parse_tests
    ahfl_smt_encode_tests
    ahfl_subset_eligibility_tests
    ahfl_smt_emit_tests
    ahfl_smt_solver_tests
    ahfl_smt_bmc_tests
    ahfl_http_transport_tests
    ahfl_grpc_transport_tests
    ahfl_wire_value_tests
    ahfl_base_json_value_tests
    ahfl_base_toml_tests
    ahfl_base_query_tests
    ahfl_compiler_frontend_queries_tests
    ahfl_compiler_query_edit_property_tests
    ahfl_compiler_manifest_tests
    ahfl_compiler_package_graph_tests
    ahfl_base_diagnostic_serialization_tests
    ahfl_base_trait_impl_diagnostics_tests
    ahfl_base_diagnostics_code_smoke_tests
    ahfl_sha256_tests
    ahfl_atomic_file_tests
    ahfl_runtime_provider_secret_provider_tests
    ahfl_vault_rotation_tests
    ahfl_pass_manager_tests
    ahfl_transform_passes_tests
    ahfl_semantics_type_relations_tests
    ahfl_semantics_type_resolver_tests
    ahfl_semantics_typed_hir_tests
    ahfl_semantics_effects_tests
    ahfl_semantics_pattern_usefulness_tests
    ahfl_semantics_diagnostic_matrix_tests
    ahfl_semantics_type_mismatch_origin_tests
    ahfl_semantics_stmt_diagnostics_tests
    ahfl_semantics_try_operator_tests
    ahfl_semantics_bounded_quantifier_tests
    ahfl_semantics_const_sema_negatives_tests
    ahfl_semantics_flow_condition_tests
    ahfl_semantics_validate_plumbing_tests
    ahfl_semantics_adt_match_tests
    ahfl_semantics_fn_generics_closures_tests
    ahfl_semantics_p2_s1_inference_tests
    ahfl_semantics_trait_impl_tests
    ahfl_semantics_concurrency_tests
    ahfl_semantics_where_clause_info_tests
    ahfl_semantics_monomorphization_tests
    ahfl_streaming_tests
    ahfl_tooling_lsp_json_rpc_tests
    ahfl_tooling_lsp_handler_tests
    ahfl_tooling_lsp_analysis_edits_tests
    ahfl_connection_pool_tests
    ahfl_error_recovery_tests
    ahfl_syntax_trait_impl_tests
    ahfl_compiler_handoff_package_tests
    ahfl_thread_pool_tests
    ahfl_version_tests
    ahfl_bmc_tests
    ahfl_model_checker_tests
    ahfl_verification_formal_integration_tests
    ahfl_bmc_depth_customization_tests
    ahfl_parallel_scheduler_tests
    ahfl_sandbox_tests
    ahfl_distributed_tests
    ahfl_tooling_formatter_tests
    ahfl_decreases_structure_tests
    ahfl_decreases_printer_tests
    ahfl_decreases_desugar_tests
    ahfl_decreases_symmetry_tests
    ahfl_enum_struct_variant_tests
    ahfl_if_let_syntax_tests
    ahfl_capacity_refinement_sugar_tests
    ahfl_inner_attribute_tests
    ahfl_tooling_repl_tests
    ahfl_tooling_dap_tests
    ahfl_tooling_telemetry_tests
    ahfl_tooling_profiling_tests
    ahfl_tooling_abi_tests
    ahfl_tooling_cache_tests
    ahfl_tooling_package_tests
    ahfl_property_lowering_tests
    ahfl_property_core_erasure_tests
    ahfl_property_smv_tests
    ahfl_compiler_ir_opt_tests
    ahfl_assurance_obligations_tests
)
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
endforeach()

if(AHFL_ENABLE_BACKEND_INFRA)
    target_include_directories(ahfl_target_backends_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()
if(AHFL_ENABLE_BACKEND_WASM)
    target_include_directories(ahfl_core_wasm_codegen_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()

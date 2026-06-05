add_executable(ahfl_project_parse_tests
    integration/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_compiler_syntax
)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    integration/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_compiler_semantics
)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    integration/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_compiler_ir
)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_compiler_ir_tests
    unit/compiler/ir/identity_visitor.cpp
)
target_link_libraries(ahfl_compiler_ir_tests
    PRIVATE
        ahfl_compiler_ir
        ahfl_compiler_handoff
        doctest
)
ahfl_apply_project_warnings(ahfl_compiler_ir_tests)

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

add_executable(ahfl_runtime_engine_session_tests
    unit/pipeline/execution/runtime_session/session.cpp
)
target_link_libraries(ahfl_runtime_engine_session_tests
    PRIVATE
        ahfl_pipeline_execution
)
ahfl_apply_project_warnings(ahfl_runtime_engine_session_tests)

add_executable(ahfl_execution_journal_tests
    unit/pipeline/execution/execution_journal/journal.cpp
)
target_link_libraries(ahfl_execution_journal_tests
    PRIVATE
        ahfl_pipeline_execution
)
ahfl_apply_project_warnings(ahfl_execution_journal_tests)

add_executable(ahfl_tooling_replay_view_tests
    unit/pipeline/execution/replay_view/replay.cpp
)
target_link_libraries(ahfl_tooling_replay_view_tests
    PRIVATE
        ahfl_pipeline_execution
)
ahfl_apply_project_warnings(ahfl_tooling_replay_view_tests)

add_executable(ahfl_audit_report_tests
    unit/pipeline/observation/audit_report/report.cpp
)
target_link_libraries(ahfl_audit_report_tests
    PRIVATE
        ahfl_pipeline_observation
)
ahfl_apply_project_warnings(ahfl_audit_report_tests)

add_executable(ahfl_scheduler_snapshot_tests
    unit/pipeline/observation/scheduler_snapshot/snapshot.cpp
)
target_link_libraries(ahfl_scheduler_snapshot_tests
    PRIVATE
        ahfl_pipeline_observation
)
ahfl_apply_project_warnings(ahfl_scheduler_snapshot_tests)

add_executable(ahfl_checkpoint_record_tests
    unit/pipeline/observation/checkpoint_record/record.cpp
)
target_link_libraries(ahfl_checkpoint_record_tests
    PRIVATE
        ahfl_pipeline_observation
)
ahfl_apply_project_warnings(ahfl_checkpoint_record_tests)

add_executable(ahfl_pipeline_persistence_descriptor_tests
    unit/pipeline/persistence/descriptor/descriptor.cpp
)
target_link_libraries(ahfl_pipeline_persistence_descriptor_tests
    PRIVATE
        ahfl_pipeline_persistence
)
ahfl_apply_project_warnings(ahfl_pipeline_persistence_descriptor_tests)

add_executable(ahfl_pipeline_persistence_export_tests
    unit/pipeline/persistence/export/manifest.cpp
)
target_link_libraries(ahfl_pipeline_persistence_export_tests
    PRIVATE
        ahfl_pipeline_persistence
)
ahfl_apply_project_warnings(ahfl_pipeline_persistence_export_tests)

add_executable(ahfl_store_import_tests
    unit/pipeline/persistence/store_import/descriptor.cpp
)
target_link_libraries(ahfl_store_import_tests
    PRIVATE
        ahfl_pipeline_persistence
)
ahfl_apply_project_warnings(ahfl_store_import_tests)

add_executable(ahfl_pipeline_durable_store_import_tests
    unit/pipeline/persistence/durable_store_import/request.cpp
)
target_link_libraries(ahfl_pipeline_durable_store_import_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
        ahfl_pipeline_persistence
)
ahfl_apply_project_warnings(ahfl_pipeline_durable_store_import_tests)

add_executable(ahfl_pipeline_durable_store_import_decision_tests
    unit/pipeline/persistence/durable_store_import/decision.cpp
)
target_link_libraries(ahfl_pipeline_durable_store_import_decision_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
        ahfl_pipeline_persistence
)
ahfl_apply_project_warnings(ahfl_pipeline_durable_store_import_decision_tests)

add_executable(ahfl_schema_compatibility_tests
    unit/pipeline/persistence/durable_store_import/provider/schema_compatibility/schema_compatibility.cpp
)
target_link_libraries(ahfl_schema_compatibility_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_schema_compatibility_tests)

add_executable(ahfl_config_bundle_validation_tests
    unit/pipeline/persistence/durable_store_import/provider/config_bundle/config_bundle_validation.cpp
)
target_link_libraries(ahfl_config_bundle_validation_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_config_bundle_validation_tests)

add_executable(ahfl_release_evidence_archive_tests
    unit/pipeline/persistence/durable_store_import/provider/release_evidence/release_evidence_archive.cpp
)
target_link_libraries(ahfl_release_evidence_archive_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_release_evidence_archive_tests)

add_executable(ahfl_approval_workflow_tests
    unit/pipeline/persistence/durable_store_import/provider/approval/approval_workflow.cpp
)
target_link_libraries(ahfl_approval_workflow_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_approval_workflow_tests)

add_executable(ahfl_opt_in_guard_tests
    unit/pipeline/persistence/durable_store_import/provider/opt_in_guard/opt_in_guard.cpp
)
target_link_libraries(ahfl_opt_in_guard_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_opt_in_guard_tests)

add_executable(ahfl_runtime_engine_policy_tests
    unit/pipeline/persistence/durable_store_import/provider/runtime_policy/runtime_policy.cpp
)
target_link_libraries(ahfl_runtime_engine_policy_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_runtime_engine_policy_tests)

add_executable(ahfl_production_integration_tests
    unit/pipeline/persistence/durable_store_import/provider/production_integration/production_integration_dry_run.cpp
)
target_link_libraries(ahfl_production_integration_tests
    PRIVATE
        ahfl_pipeline_durable_store_import
)
ahfl_apply_project_warnings(ahfl_production_integration_tests)

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

add_executable(ahfl_capability_bridge_tests
    unit/runtime/engine/capability_bridge.cpp
)
target_link_libraries(ahfl_capability_bridge_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_capability_bridge_tests)

add_executable(ahfl_e2e_workflow_tests
    unit/runtime/engine/e2e_workflow.cpp
)
target_link_libraries(ahfl_e2e_workflow_tests
    PRIVATE
        ahfl_runtime_engine
)
ahfl_apply_project_warnings(ahfl_e2e_workflow_tests)

add_executable(ahfl_runtime_provider_llm_tests
    unit/runtime/providers/llm/llm_provider.cpp
)
target_link_libraries(ahfl_runtime_provider_llm_tests
    PRIVATE
        ahfl_runtime_provider_llm
)
ahfl_apply_project_warnings(ahfl_runtime_provider_llm_tests)

add_executable(ahfl_value_json_tests
    unit/runtime/evaluator/value_json.cpp
)
target_link_libraries(ahfl_value_json_tests
    PRIVATE
        ahfl_runtime_evaluator
)
ahfl_apply_project_warnings(ahfl_value_json_tests)

add_executable(ahfl_counterexample_parse_tests
    unit/verification/formal/counterexample_parse.cpp
)
target_link_libraries(ahfl_counterexample_parse_tests
    PRIVATE
        ahfl_verification_formal
)
ahfl_apply_project_warnings(ahfl_counterexample_parse_tests)

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

add_executable(ahfl_base_json_value_tests
    unit/base/json/json_value.cpp
)
target_link_libraries(ahfl_base_json_value_tests
    PRIVATE
        ahfl_base_json
)
ahfl_apply_project_warnings(ahfl_base_json_value_tests)

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

add_executable(ahfl_thread_pool_tests
    unit/base/support/thread_pool.cpp
)
target_link_libraries(ahfl_thread_pool_tests
    PRIVATE
        ahfl_base_support
)
ahfl_apply_project_warnings(ahfl_thread_pool_tests)

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

add_executable(ahfl_tooling_incremental_tests
    unit/tooling/incremental/incremental.cpp
)
target_link_libraries(ahfl_tooling_incremental_tests
    PRIVATE
        ahfl_tooling_incremental
)
ahfl_apply_project_warnings(ahfl_tooling_incremental_tests)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_executable(ahfl_wasm_backend_tests
        unit/compiler/backends/wasm_backend.cpp
    )
    target_link_libraries(ahfl_wasm_backend_tests
        PRIVATE
            ahfl_compiler_backend_infra_wasm
    )
    ahfl_apply_project_warnings(ahfl_wasm_backend_tests)
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
        ahfl_pipeline_durable_store_import
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
)
ahfl_apply_project_warnings(ahfl_property_lowering_tests)

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
    ahfl_runtime_engine_session_tests
    ahfl_execution_journal_tests
    ahfl_tooling_replay_view_tests
    ahfl_audit_report_tests
    ahfl_scheduler_snapshot_tests
    ahfl_checkpoint_record_tests
    ahfl_pipeline_persistence_descriptor_tests
    ahfl_pipeline_persistence_export_tests
    ahfl_store_import_tests
    ahfl_pipeline_durable_store_import_tests
    ahfl_pipeline_durable_store_import_decision_tests
    ahfl_schema_compatibility_tests
    ahfl_config_bundle_validation_tests
    ahfl_release_evidence_archive_tests
    ahfl_approval_workflow_tests
    ahfl_opt_in_guard_tests
    ahfl_runtime_engine_policy_tests
    ahfl_production_integration_tests
    ahfl_compiler_handoff_package_compat_tests
    ahfl_compiler_backends_registry_tests
    ahfl_cli_command_routing_tests
    ahfl_runtime_evaluator_tests
    ahfl_executor_tests
    ahfl_agent_runtime_tests
    ahfl_workflow_runtime_tests
    ahfl_capability_bridge_tests
    ahfl_e2e_workflow_tests
    ahfl_runtime_provider_llm_tests
    ahfl_value_json_tests
    ahfl_counterexample_parse_tests
    ahfl_http_transport_tests
    ahfl_grpc_transport_tests
    ahfl_base_json_value_tests
    ahfl_runtime_provider_secret_provider_tests
    ahfl_vault_rotation_tests
    ahfl_pass_manager_tests
    ahfl_streaming_tests
    ahfl_tooling_lsp_json_rpc_tests
    ahfl_tooling_lsp_handler_tests
    ahfl_connection_pool_tests
    ahfl_error_recovery_tests
    ahfl_compiler_handoff_package_tests
    ahfl_thread_pool_tests
    ahfl_version_tests
    ahfl_bmc_tests
    ahfl_model_checker_tests
    ahfl_verification_formal_integration_tests
    ahfl_parallel_scheduler_tests
    ahfl_sandbox_tests
    ahfl_distributed_tests
    ahfl_tooling_formatter_tests
    ahfl_tooling_repl_tests
    ahfl_tooling_dap_tests
    ahfl_tooling_telemetry_tests
    ahfl_tooling_profiling_tests
    ahfl_tooling_abi_tests
    ahfl_tooling_incremental_tests
    ahfl_tooling_package_tests
    ahfl_property_lowering_tests
    ahfl_property_smv_tests
)
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
endforeach()

if(AHFL_ENABLE_BACKEND_INFRA)
    foreach(_tgt ahfl_target_backends_tests ahfl_wasm_backend_tests)
        target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    endforeach()
endif()

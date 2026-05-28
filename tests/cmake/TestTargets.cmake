add_executable(ahfl_project_parse_tests
    integration/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    integration/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_semantics
)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    integration/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_ir
)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_handoff_package_tests
    unit/handoff/package_model.cpp
)
target_link_libraries(ahfl_handoff_package_tests
    PRIVATE
        ahfl_handoff
)
ahfl_apply_project_warnings(ahfl_handoff_package_tests)

add_executable(ahfl_handoff_package_compat_tests
    unit/handoff/package_compat.cpp
)
target_link_libraries(ahfl_handoff_package_compat_tests
    PRIVATE
        ahfl_backend_pipeline_handoff
)
ahfl_apply_project_warnings(ahfl_handoff_package_compat_tests)

add_executable(ahfl_dry_run_tests
    unit/dry_run/runner.cpp
)
target_link_libraries(ahfl_dry_run_tests
    PRIVATE
        ahfl_execution_pipeline
)
ahfl_apply_project_warnings(ahfl_dry_run_tests)

add_executable(ahfl_runtime_session_tests
    unit/runtime_session/session.cpp
)
target_link_libraries(ahfl_runtime_session_tests
    PRIVATE
        ahfl_execution_pipeline
)
ahfl_apply_project_warnings(ahfl_runtime_session_tests)

add_executable(ahfl_execution_journal_tests
    unit/execution_journal/journal.cpp
)
target_link_libraries(ahfl_execution_journal_tests
    PRIVATE
        ahfl_execution_pipeline
)
ahfl_apply_project_warnings(ahfl_execution_journal_tests)

add_executable(ahfl_replay_view_tests
    unit/replay_view/replay.cpp
)
target_link_libraries(ahfl_replay_view_tests
    PRIVATE
        ahfl_execution_pipeline
)
ahfl_apply_project_warnings(ahfl_replay_view_tests)

add_executable(ahfl_audit_report_tests
    unit/audit_report/report.cpp
)
target_link_libraries(ahfl_audit_report_tests
    PRIVATE
        ahfl_observation
)
ahfl_apply_project_warnings(ahfl_audit_report_tests)

add_executable(ahfl_scheduler_snapshot_tests
    unit/scheduler_snapshot/snapshot.cpp
)
target_link_libraries(ahfl_scheduler_snapshot_tests
    PRIVATE
        ahfl_observation
)
ahfl_apply_project_warnings(ahfl_scheduler_snapshot_tests)

add_executable(ahfl_checkpoint_record_tests
    unit/checkpoint_record/record.cpp
)
target_link_libraries(ahfl_checkpoint_record_tests
    PRIVATE
        ahfl_observation
)
ahfl_apply_project_warnings(ahfl_checkpoint_record_tests)

add_executable(ahfl_persistence_descriptor_tests
    unit/persistence_descriptor/descriptor.cpp
)
target_link_libraries(ahfl_persistence_descriptor_tests
    PRIVATE
        ahfl_persistence
)
ahfl_apply_project_warnings(ahfl_persistence_descriptor_tests)

add_executable(ahfl_persistence_export_tests
    unit/persistence_export/manifest.cpp
)
target_link_libraries(ahfl_persistence_export_tests
    PRIVATE
        ahfl_persistence
)
ahfl_apply_project_warnings(ahfl_persistence_export_tests)

add_executable(ahfl_store_import_tests
    unit/store_import/descriptor.cpp
)
target_link_libraries(ahfl_store_import_tests
    PRIVATE
        ahfl_persistence
)
ahfl_apply_project_warnings(ahfl_store_import_tests)

add_executable(ahfl_durable_store_import_tests
    unit/durable_store_import/request.cpp
)
target_link_libraries(ahfl_durable_store_import_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_persistence
)
ahfl_apply_project_warnings(ahfl_durable_store_import_tests)

add_executable(ahfl_durable_store_import_decision_tests
    unit/durable_store_import/decision.cpp
)
target_link_libraries(ahfl_durable_store_import_decision_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_persistence
)
ahfl_apply_project_warnings(ahfl_durable_store_import_decision_tests)

add_executable(ahfl_schema_compatibility_tests
    unit/schema_compatibility/schema_compatibility.cpp
)
target_link_libraries(ahfl_schema_compatibility_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_schema_compatibility_tests)

add_executable(ahfl_config_bundle_validation_tests
    unit/config_bundle/config_bundle_validation.cpp
)
target_link_libraries(ahfl_config_bundle_validation_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_config_bundle_validation_tests)

add_executable(ahfl_release_evidence_archive_tests
    unit/release_evidence/release_evidence_archive.cpp
)
target_link_libraries(ahfl_release_evidence_archive_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_release_evidence_archive_tests)

add_executable(ahfl_approval_workflow_tests
    unit/approval/approval_workflow.cpp
)
target_link_libraries(ahfl_approval_workflow_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_approval_workflow_tests)

add_executable(ahfl_opt_in_guard_tests
    unit/opt_in_guard/opt_in_guard.cpp
)
target_link_libraries(ahfl_opt_in_guard_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_opt_in_guard_tests)

add_executable(ahfl_runtime_policy_tests
    unit/runtime_policy/runtime_policy.cpp
)
target_link_libraries(ahfl_runtime_policy_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_runtime_policy_tests)

add_executable(ahfl_production_integration_tests
    unit/production_integration/production_integration_dry_run.cpp
)
target_link_libraries(ahfl_production_integration_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_production_integration_tests)

add_executable(ahfl_evaluator_tests
    unit/evaluator/evaluator.cpp
)
target_link_libraries(ahfl_evaluator_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_evaluator_tests)

add_executable(ahfl_executor_tests
    unit/evaluator/executor.cpp
)
target_link_libraries(ahfl_executor_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_executor_tests)

add_executable(ahfl_agent_runtime_tests
    unit/runtime/agent_runtime.cpp
)
target_link_libraries(ahfl_agent_runtime_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_agent_runtime_tests)

add_executable(ahfl_workflow_runtime_tests
    unit/runtime/workflow_runtime.cpp
)
target_link_libraries(ahfl_workflow_runtime_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_workflow_runtime_tests)

add_executable(ahfl_capability_bridge_tests
    unit/runtime/capability_bridge.cpp
)
target_link_libraries(ahfl_capability_bridge_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_capability_bridge_tests)

add_executable(ahfl_e2e_workflow_tests
    unit/runtime/e2e_workflow.cpp
)
target_link_libraries(ahfl_e2e_workflow_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_e2e_workflow_tests)

add_executable(ahfl_llm_provider_tests
    unit/llm_provider/llm_provider.cpp
)
target_link_libraries(ahfl_llm_provider_tests
    PRIVATE
        ahfl_llm_provider
)
ahfl_apply_project_warnings(ahfl_llm_provider_tests)

add_executable(ahfl_value_json_tests
    unit/evaluator/value_json.cpp
)
target_link_libraries(ahfl_value_json_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_value_json_tests)

add_executable(ahfl_counterexample_parse_tests
    unit/formal/counterexample_parse.cpp
)
target_link_libraries(ahfl_counterexample_parse_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_counterexample_parse_tests)

add_executable(ahfl_http_transport_tests
    unit/runtime/http_transport.cpp
)
target_link_libraries(ahfl_http_transport_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_http_transport_tests)

add_executable(ahfl_grpc_transport_tests
    unit/runtime/grpc_transport.cpp
)
target_link_libraries(ahfl_grpc_transport_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_grpc_transport_tests)

add_executable(ahfl_json_value_tests
    unit/json/json_value.cpp
)
target_link_libraries(ahfl_json_value_tests
    PRIVATE
        ahfl_json
)
ahfl_apply_project_warnings(ahfl_json_value_tests)

add_executable(ahfl_secret_provider_tests
    unit/secret/secret_provider.cpp
)
target_link_libraries(ahfl_secret_provider_tests
    PRIVATE
        ahfl_secret
)
ahfl_apply_project_warnings(ahfl_secret_provider_tests)

add_executable(ahfl_vault_rotation_tests
    unit/secret/vault_rotation.cpp
)
target_link_libraries(ahfl_vault_rotation_tests
    PRIVATE
        ahfl_secret
)
ahfl_apply_project_warnings(ahfl_vault_rotation_tests)

add_executable(ahfl_pass_manager_tests
    unit/passes/pass_manager.cpp
)
target_link_libraries(ahfl_pass_manager_tests
    PRIVATE
        ahfl_passes
        doctest
)
ahfl_apply_project_warnings(ahfl_pass_manager_tests)

add_executable(ahfl_streaming_tests
    unit/llm_provider/streaming.cpp
)
target_link_libraries(ahfl_streaming_tests
    PRIVATE
        ahfl_llm_provider
)
ahfl_apply_project_warnings(ahfl_streaming_tests)

add_executable(ahfl_lsp_json_rpc_tests
    unit/lsp/json_rpc.cpp
)
target_link_libraries(ahfl_lsp_json_rpc_tests
    PRIVATE
        ahfl_lsp
)
ahfl_apply_project_warnings(ahfl_lsp_json_rpc_tests)

add_executable(ahfl_lsp_handler_tests
    unit/lsp/server_handlers.cpp
)
target_link_libraries(ahfl_lsp_handler_tests
    PRIVATE
        ahfl_lsp
)
ahfl_apply_project_warnings(ahfl_lsp_handler_tests)

add_executable(ahfl_connection_pool_tests
    unit/runtime/connection_pool.cpp
)
target_link_libraries(ahfl_connection_pool_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_connection_pool_tests)

add_executable(ahfl_error_recovery_tests
    unit/frontend/error_recovery.cpp
)
target_link_libraries(ahfl_error_recovery_tests
    PRIVATE
        ahfl_frontend_lib
)
ahfl_apply_project_warnings(ahfl_error_recovery_tests)

add_executable(ahfl_thread_pool_tests
    unit/support/thread_pool.cpp
)
target_link_libraries(ahfl_thread_pool_tests
    PRIVATE
        ahfl_support_lib
)
ahfl_apply_project_warnings(ahfl_thread_pool_tests)

add_executable(ahfl_version_tests
    unit/support/version.cpp
)
target_link_libraries(ahfl_version_tests
    PRIVATE
        ahfl_support_lib
)
ahfl_apply_project_warnings(ahfl_version_tests)

add_executable(ahfl_bmc_tests
    unit/formal/bmc.cpp
)
target_link_libraries(ahfl_bmc_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_bmc_tests)

add_executable(ahfl_model_checker_tests
    unit/formal/model_checker_backends.cpp
)
target_link_libraries(ahfl_model_checker_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_model_checker_tests)

add_executable(ahfl_formal_integration_tests
    unit/formal/integration_improvement.cpp
)
target_link_libraries(ahfl_formal_integration_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_formal_integration_tests)

add_executable(ahfl_parallel_scheduler_tests
    unit/runtime/parallel_scheduler.cpp
)
target_link_libraries(ahfl_parallel_scheduler_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_parallel_scheduler_tests)

add_executable(ahfl_sandbox_tests
    unit/runtime/sandbox.cpp
)
target_link_libraries(ahfl_sandbox_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_sandbox_tests)

add_executable(ahfl_distributed_tests
    unit/runtime/distributed.cpp
)
target_link_libraries(ahfl_distributed_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_distributed_tests)

add_executable(ahfl_formatter_tests
    unit/formatter/formatter.cpp
)
target_link_libraries(ahfl_formatter_tests
    PRIVATE
        ahfl_formatter
)
ahfl_apply_project_warnings(ahfl_formatter_tests)

add_executable(ahfl_repl_tests
    unit/repl/repl.cpp
)
target_link_libraries(ahfl_repl_tests
    PRIVATE
        ahfl_repl
)
ahfl_apply_project_warnings(ahfl_repl_tests)

add_executable(ahfl_dap_tests
    unit/dap/dap_basic.cpp
)
target_link_libraries(ahfl_dap_tests
    PRIVATE
        ahfl_dap
)
ahfl_apply_project_warnings(ahfl_dap_tests)

add_executable(ahfl_telemetry_tests
    unit/telemetry/telemetry.cpp
)
target_link_libraries(ahfl_telemetry_tests
    PRIVATE
        ahfl_telemetry
)
ahfl_apply_project_warnings(ahfl_telemetry_tests)

add_executable(ahfl_profiling_tests
    unit/profiling/profiling.cpp
)
target_link_libraries(ahfl_profiling_tests
    PRIVATE
        ahfl_profiling
)
ahfl_apply_project_warnings(ahfl_profiling_tests)

add_executable(ahfl_abi_tests
    unit/abi/abi_compat.cpp
)
target_link_libraries(ahfl_abi_tests
    PRIVATE
        ahfl_abi
)
ahfl_apply_project_warnings(ahfl_abi_tests)

add_executable(ahfl_incremental_tests
    unit/incremental/incremental.cpp
)
target_link_libraries(ahfl_incremental_tests
    PRIVATE
        ahfl_incremental
)
ahfl_apply_project_warnings(ahfl_incremental_tests)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_executable(ahfl_wasm_backend_tests
        unit/backends/wasm_backend.cpp
    )
    target_link_libraries(ahfl_wasm_backend_tests
        PRIVATE
            ahfl_backend_wasm
    )
    ahfl_apply_project_warnings(ahfl_wasm_backend_tests)
endif()

add_executable(ahfl_backend_registry_tests
    unit/backends/registry.cpp
)
target_link_libraries(ahfl_backend_registry_tests
    PRIVATE
        ahfl_backend
)
ahfl_apply_project_warnings(ahfl_backend_registry_tests)

add_executable(ahfl_cli_command_routing_tests
    unit/cli/command_routing.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/command_catalog.cpp
)
target_link_libraries(ahfl_cli_command_routing_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_support
)
ahfl_apply_project_warnings(ahfl_cli_command_routing_tests)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_executable(ahfl_target_backends_tests
        unit/backends/target_backends.cpp
    )
    target_link_libraries(ahfl_target_backends_tests
        PRIVATE
            ahfl_backend_k8s_crd
            ahfl_backend_openapi_spec
            ahfl_backend_terraform_gen
    )
    ahfl_apply_project_warnings(ahfl_target_backends_tests)
endif()

add_executable(ahfl_package_tests
    unit/package/package.cpp
)
target_link_libraries(ahfl_package_tests
    PRIVATE
        ahfl_package
)
ahfl_apply_project_warnings(ahfl_package_tests)

add_executable(ahfl_property_lowering_tests
    unit/property/lowering_equiv.cpp
)
target_link_libraries(ahfl_property_lowering_tests
    PRIVATE
        ahfl_testing
)
ahfl_apply_project_warnings(ahfl_property_lowering_tests)

add_executable(ahfl_property_smv_tests
    unit/property/smv_syntax.cpp
)
target_link_libraries(ahfl_property_smv_tests
    PRIVATE
        ahfl_testing
)
ahfl_apply_project_warnings(ahfl_property_smv_tests)

# Test targets that directly include internal src/ headers need PRIVATE access
foreach(_tgt
    ahfl_dry_run_tests
    ahfl_runtime_session_tests
    ahfl_execution_journal_tests
    ahfl_replay_view_tests
    ahfl_audit_report_tests
    ahfl_scheduler_snapshot_tests
    ahfl_checkpoint_record_tests
    ahfl_persistence_descriptor_tests
    ahfl_persistence_export_tests
    ahfl_store_import_tests
    ahfl_durable_store_import_tests
    ahfl_durable_store_import_decision_tests
    ahfl_schema_compatibility_tests
    ahfl_config_bundle_validation_tests
    ahfl_release_evidence_archive_tests
    ahfl_approval_workflow_tests
    ahfl_opt_in_guard_tests
    ahfl_runtime_policy_tests
    ahfl_production_integration_tests
    ahfl_handoff_package_compat_tests
    ahfl_backend_registry_tests
    ahfl_cli_command_routing_tests
    ahfl_evaluator_tests
    ahfl_executor_tests
    ahfl_agent_runtime_tests
    ahfl_workflow_runtime_tests
    ahfl_capability_bridge_tests
    ahfl_e2e_workflow_tests
    ahfl_llm_provider_tests
    ahfl_value_json_tests
    ahfl_counterexample_parse_tests
    ahfl_http_transport_tests
    ahfl_grpc_transport_tests
    ahfl_json_value_tests
    ahfl_secret_provider_tests
    ahfl_vault_rotation_tests
    ahfl_pass_manager_tests
    ahfl_streaming_tests
    ahfl_lsp_json_rpc_tests
    ahfl_lsp_handler_tests
    ahfl_connection_pool_tests
    ahfl_error_recovery_tests
    ahfl_thread_pool_tests
    ahfl_version_tests
    ahfl_bmc_tests
    ahfl_model_checker_tests
    ahfl_formal_integration_tests
    ahfl_parallel_scheduler_tests
    ahfl_sandbox_tests
    ahfl_distributed_tests
    ahfl_formatter_tests
    ahfl_repl_tests
    ahfl_dap_tests
    ahfl_telemetry_tests
    ahfl_profiling_tests
    ahfl_abi_tests
    ahfl_incremental_tests
    ahfl_package_tests
    ahfl_property_lowering_tests
    ahfl_property_smv_tests
)
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
endforeach()

if(AHFL_ENABLE_BACKEND_INFRA)
    foreach(_tgt ahfl_target_backends_tests ahfl_wasm_backend_tests)
        target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    endforeach()
endif()

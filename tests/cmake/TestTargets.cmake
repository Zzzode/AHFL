add_executable(ahfl_project_parse_tests
    project/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    project/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_semantics
)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    project/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_ir
)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_handoff_package_tests
    handoff/package_model.cpp
)
target_link_libraries(ahfl_handoff_package_tests
    PRIVATE
        ahfl_handoff
)
ahfl_apply_project_warnings(ahfl_handoff_package_tests)

add_executable(ahfl_handoff_package_compat_tests
    handoff/package_compat.cpp
)
target_link_libraries(ahfl_handoff_package_compat_tests
    PRIVATE
        ahfl_backend_native_json
)
ahfl_apply_project_warnings(ahfl_handoff_package_compat_tests)

add_executable(ahfl_dry_run_tests
    dry_run/runner.cpp
)
target_link_libraries(ahfl_dry_run_tests
    PRIVATE
        ahfl_dry_run
)
ahfl_apply_project_warnings(ahfl_dry_run_tests)

add_executable(ahfl_runtime_session_tests
    runtime_session/session.cpp
)
target_link_libraries(ahfl_runtime_session_tests
    PRIVATE
        ahfl_runtime_session
)
ahfl_apply_project_warnings(ahfl_runtime_session_tests)

add_executable(ahfl_execution_journal_tests
    execution_journal/journal.cpp
)
target_link_libraries(ahfl_execution_journal_tests
    PRIVATE
        ahfl_execution_journal
)
ahfl_apply_project_warnings(ahfl_execution_journal_tests)

add_executable(ahfl_replay_view_tests
    replay_view/replay.cpp
)
target_link_libraries(ahfl_replay_view_tests
    PRIVATE
        ahfl_replay_view
)
ahfl_apply_project_warnings(ahfl_replay_view_tests)

add_executable(ahfl_audit_report_tests
    audit_report/report.cpp
)
target_link_libraries(ahfl_audit_report_tests
    PRIVATE
        ahfl_audit_report
)
ahfl_apply_project_warnings(ahfl_audit_report_tests)

add_executable(ahfl_scheduler_snapshot_tests
    scheduler_snapshot/snapshot.cpp
)
target_link_libraries(ahfl_scheduler_snapshot_tests
    PRIVATE
        ahfl_scheduler_snapshot
)
ahfl_apply_project_warnings(ahfl_scheduler_snapshot_tests)

add_executable(ahfl_checkpoint_record_tests
    checkpoint_record/record.cpp
)
target_link_libraries(ahfl_checkpoint_record_tests
    PRIVATE
        ahfl_checkpoint_record
)
ahfl_apply_project_warnings(ahfl_checkpoint_record_tests)

add_executable(ahfl_persistence_descriptor_tests
    persistence_descriptor/descriptor.cpp
)
target_link_libraries(ahfl_persistence_descriptor_tests
    PRIVATE
        ahfl_persistence_descriptor
)
ahfl_apply_project_warnings(ahfl_persistence_descriptor_tests)

add_executable(ahfl_persistence_export_tests
    persistence_export/manifest.cpp
)
target_link_libraries(ahfl_persistence_export_tests
    PRIVATE
        ahfl_persistence_export
)
ahfl_apply_project_warnings(ahfl_persistence_export_tests)

add_executable(ahfl_store_import_tests
    store_import/descriptor.cpp
)
target_link_libraries(ahfl_store_import_tests
    PRIVATE
        ahfl_store_import
        ahfl_persistence_export
)
ahfl_apply_project_warnings(ahfl_store_import_tests)

add_executable(ahfl_durable_store_import_tests
    durable_store_import/request.cpp
)
target_link_libraries(ahfl_durable_store_import_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_store_import
)
ahfl_apply_project_warnings(ahfl_durable_store_import_tests)

add_executable(ahfl_durable_store_import_decision_tests
    durable_store_import/decision.cpp
)
target_link_libraries(ahfl_durable_store_import_decision_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_store_import
)
ahfl_apply_project_warnings(ahfl_durable_store_import_decision_tests)

add_executable(ahfl_schema_compatibility_tests
    schema_compatibility/schema_compatibility.cpp
)
target_link_libraries(ahfl_schema_compatibility_tests
    PRIVATE
        ahfl_durable_store_import_artifacts
)
ahfl_apply_project_warnings(ahfl_schema_compatibility_tests)

add_executable(ahfl_config_bundle_validation_tests
    config_bundle/config_bundle_validation.cpp
)
target_link_libraries(ahfl_config_bundle_validation_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_config_bundle_validation_tests)

add_executable(ahfl_release_evidence_archive_tests
    release_evidence/release_evidence_archive.cpp
)
target_link_libraries(ahfl_release_evidence_archive_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_release_evidence_archive_tests)

add_executable(ahfl_approval_workflow_tests
    approval/approval_workflow.cpp
)
target_link_libraries(ahfl_approval_workflow_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_approval_workflow_tests)

add_executable(ahfl_opt_in_guard_tests
    opt_in_guard/opt_in_guard.cpp
)
target_link_libraries(ahfl_opt_in_guard_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_opt_in_guard_tests)

add_executable(ahfl_runtime_policy_tests
    runtime_policy/runtime_policy.cpp
)
target_link_libraries(ahfl_runtime_policy_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_runtime_policy_tests)

add_executable(ahfl_production_integration_tests
    production_integration/production_integration_dry_run.cpp
)
target_link_libraries(ahfl_production_integration_tests
    PRIVATE
        ahfl_durable_store_import
)
ahfl_apply_project_warnings(ahfl_production_integration_tests)

add_executable(ahfl_evaluator_tests
    evaluator/evaluator.cpp
)
target_link_libraries(ahfl_evaluator_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_evaluator_tests)

add_executable(ahfl_executor_tests
    evaluator/executor.cpp
)
target_link_libraries(ahfl_executor_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_executor_tests)

add_executable(ahfl_agent_runtime_tests
    runtime/agent_runtime.cpp
)
target_link_libraries(ahfl_agent_runtime_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_agent_runtime_tests)

add_executable(ahfl_workflow_runtime_tests
    runtime/workflow_runtime.cpp
)
target_link_libraries(ahfl_workflow_runtime_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_workflow_runtime_tests)

add_executable(ahfl_capability_bridge_tests
    runtime/capability_bridge.cpp
)
target_link_libraries(ahfl_capability_bridge_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_capability_bridge_tests)

add_executable(ahfl_e2e_workflow_tests
    runtime/e2e_workflow.cpp
)
target_link_libraries(ahfl_e2e_workflow_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_e2e_workflow_tests)

add_executable(ahfl_llm_provider_tests
    llm_provider/llm_provider.cpp
)
target_link_libraries(ahfl_llm_provider_tests
    PRIVATE
        ahfl_llm_provider
)
ahfl_apply_project_warnings(ahfl_llm_provider_tests)

add_executable(ahfl_value_json_tests
    evaluator/value_json.cpp
)
target_link_libraries(ahfl_value_json_tests
    PRIVATE
        ahfl_evaluator
)
ahfl_apply_project_warnings(ahfl_value_json_tests)

add_executable(ahfl_counterexample_parse_tests
    formal/counterexample_parse.cpp
)
target_link_libraries(ahfl_counterexample_parse_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_counterexample_parse_tests)

add_executable(ahfl_http_transport_tests
    runtime/http_transport.cpp
)
target_link_libraries(ahfl_http_transport_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_http_transport_tests)

add_executable(ahfl_grpc_transport_tests
    runtime/grpc_transport.cpp
)
target_link_libraries(ahfl_grpc_transport_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_grpc_transport_tests)

add_executable(ahfl_json_value_tests
    json/json_value.cpp
)
target_link_libraries(ahfl_json_value_tests
    PRIVATE
        ahfl_json
)
ahfl_apply_project_warnings(ahfl_json_value_tests)

add_executable(ahfl_secret_provider_tests
    secret/secret_provider.cpp
)
target_link_libraries(ahfl_secret_provider_tests
    PRIVATE
        ahfl_secret
)
ahfl_apply_project_warnings(ahfl_secret_provider_tests)

add_executable(ahfl_vault_rotation_tests
    secret/vault_rotation.cpp
)
target_link_libraries(ahfl_vault_rotation_tests
    PRIVATE
        ahfl_secret
)
ahfl_apply_project_warnings(ahfl_vault_rotation_tests)

add_executable(ahfl_pass_manager_tests
    passes/pass_manager.cpp
)
target_link_libraries(ahfl_pass_manager_tests
    PRIVATE
        ahfl_passes
)
ahfl_apply_project_warnings(ahfl_pass_manager_tests)

add_executable(ahfl_streaming_tests
    llm_provider/streaming.cpp
)
target_link_libraries(ahfl_streaming_tests
    PRIVATE
        ahfl_llm_provider
)
ahfl_apply_project_warnings(ahfl_streaming_tests)

add_executable(ahfl_lsp_json_rpc_tests
    lsp/json_rpc.cpp
)
target_link_libraries(ahfl_lsp_json_rpc_tests
    PRIVATE
        ahfl_lsp
)
ahfl_apply_project_warnings(ahfl_lsp_json_rpc_tests)

add_executable(ahfl_lsp_handler_tests
    lsp/server_handlers.cpp
)
target_link_libraries(ahfl_lsp_handler_tests
    PRIVATE
        ahfl_lsp
)
ahfl_apply_project_warnings(ahfl_lsp_handler_tests)

add_executable(ahfl_connection_pool_tests
    runtime/connection_pool.cpp
)
target_link_libraries(ahfl_connection_pool_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_connection_pool_tests)

add_executable(ahfl_error_recovery_tests
    frontend/error_recovery.cpp
)
target_link_libraries(ahfl_error_recovery_tests
    PRIVATE
        ahfl_frontend_lib
)
ahfl_apply_project_warnings(ahfl_error_recovery_tests)

add_executable(ahfl_thread_pool_tests
    support/thread_pool.cpp
)
target_link_libraries(ahfl_thread_pool_tests
    PRIVATE
        ahfl_support_lib
)
ahfl_apply_project_warnings(ahfl_thread_pool_tests)

add_executable(ahfl_version_tests
    support/version.cpp
)
target_link_libraries(ahfl_version_tests
    PRIVATE
        ahfl_support_lib
)
ahfl_apply_project_warnings(ahfl_version_tests)

add_executable(ahfl_bmc_tests
    formal/bmc.cpp
)
target_link_libraries(ahfl_bmc_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_bmc_tests)

add_executable(ahfl_model_checker_tests
    formal/model_checker_backends.cpp
)
target_link_libraries(ahfl_model_checker_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_model_checker_tests)

add_executable(ahfl_formal_integration_tests
    formal/integration_improvement.cpp
)
target_link_libraries(ahfl_formal_integration_tests
    PRIVATE
        ahfl_formal
)
ahfl_apply_project_warnings(ahfl_formal_integration_tests)

add_executable(ahfl_parallel_scheduler_tests
    runtime/parallel_scheduler.cpp
)
target_link_libraries(ahfl_parallel_scheduler_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_parallel_scheduler_tests)

add_executable(ahfl_sandbox_tests
    runtime/sandbox.cpp
)
target_link_libraries(ahfl_sandbox_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_sandbox_tests)

add_executable(ahfl_distributed_tests
    runtime/distributed.cpp
)
target_link_libraries(ahfl_distributed_tests
    PRIVATE
        ahfl_runtime
)
ahfl_apply_project_warnings(ahfl_distributed_tests)

add_executable(ahfl_formatter_tests
    formatter/formatter.cpp
)
target_link_libraries(ahfl_formatter_tests
    PRIVATE
        ahfl_formatter
)
ahfl_apply_project_warnings(ahfl_formatter_tests)

add_executable(ahfl_repl_tests
    repl/repl.cpp
)
target_link_libraries(ahfl_repl_tests
    PRIVATE
        ahfl_repl
)
ahfl_apply_project_warnings(ahfl_repl_tests)

add_executable(ahfl_dap_tests
    dap/dap_basic.cpp
)
target_link_libraries(ahfl_dap_tests
    PRIVATE
        ahfl_dap
)
ahfl_apply_project_warnings(ahfl_dap_tests)

add_executable(ahfl_telemetry_tests
    telemetry/telemetry.cpp
)
target_link_libraries(ahfl_telemetry_tests
    PRIVATE
        ahfl_telemetry
)
ahfl_apply_project_warnings(ahfl_telemetry_tests)

add_executable(ahfl_profiling_tests
    profiling/profiling.cpp
)
target_link_libraries(ahfl_profiling_tests
    PRIVATE
        ahfl_profiling
)
ahfl_apply_project_warnings(ahfl_profiling_tests)

add_executable(ahfl_abi_tests
    abi/abi_compat.cpp
)
target_link_libraries(ahfl_abi_tests
    PRIVATE
        ahfl_abi
)
ahfl_apply_project_warnings(ahfl_abi_tests)

add_executable(ahfl_incremental_tests
    incremental/incremental.cpp
)
target_link_libraries(ahfl_incremental_tests
    PRIVATE
        ahfl_incremental
)
ahfl_apply_project_warnings(ahfl_incremental_tests)

add_executable(ahfl_wasm_backend_tests
    backends/wasm_backend.cpp
)
target_link_libraries(ahfl_wasm_backend_tests
    PRIVATE
        ahfl_backend_wasm
)
ahfl_apply_project_warnings(ahfl_wasm_backend_tests)

add_executable(ahfl_backend_registry_tests
    backends/registry.cpp
)
target_link_libraries(ahfl_backend_registry_tests
    PRIVATE
        ahfl_backend
)
ahfl_apply_project_warnings(ahfl_backend_registry_tests)

add_executable(ahfl_cli_command_routing_tests
    cli/command_routing.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/command_catalog.cpp
)
target_link_libraries(ahfl_cli_command_routing_tests
    PRIVATE
        ahfl_durable_store_import_artifacts
        ahfl_support
)
ahfl_apply_project_warnings(ahfl_cli_command_routing_tests)

add_executable(ahfl_target_backends_tests
    backends/target_backends.cpp
)
target_link_libraries(ahfl_target_backends_tests
    PRIVATE
        ahfl_backend_k8s_crd
        ahfl_backend_openapi_spec
        ahfl_backend_terraform_gen
)
ahfl_apply_project_warnings(ahfl_target_backends_tests)

add_executable(ahfl_package_tests
    package/package.cpp
)
target_link_libraries(ahfl_package_tests
    PRIVATE
        ahfl_package
)
ahfl_apply_project_warnings(ahfl_package_tests)

add_executable(ahfl_property_lowering_tests
    property/lowering_equiv.cpp
)
target_link_libraries(ahfl_property_lowering_tests
    PRIVATE
        ahfl_testing
)
ahfl_apply_project_warnings(ahfl_property_lowering_tests)

add_executable(ahfl_property_smv_tests
    property/smv_syntax.cpp
)
target_link_libraries(ahfl_property_smv_tests
    PRIVATE
        ahfl_testing
)
ahfl_apply_project_warnings(ahfl_property_smv_tests)

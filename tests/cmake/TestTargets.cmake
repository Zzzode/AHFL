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
        ahfl_ir
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
        ahfl_durable_store_import
        ahfl_backend_durable_store_import_provider_schema_compatibility_report
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
        ahfl_ir
        ahfl_semantics
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_e2e_workflow_tests)

add_executable(ahfl_llm_provider_tests
    llm_provider/llm_provider.cpp
)
target_link_libraries(ahfl_llm_provider_tests
    PRIVATE
        ahfl_llm_provider
        ahfl_ir
        ahfl_semantics
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_llm_provider_tests)

add_library(ahfl_backend_smv STATIC
    smv.cpp
)

target_link_libraries(ahfl_backend_smv
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_smv)

add_library(ahfl_backend_summary STATIC
    summary.cpp
)

target_link_libraries(ahfl_backend_summary
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_summary)

add_library(ahfl_backend_native_json STATIC
    native_json.cpp
)

target_link_libraries(ahfl_backend_native_json
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_native_json)

add_library(ahfl_backend_execution_plan STATIC
    execution_plan.cpp
)

target_link_libraries(ahfl_backend_execution_plan
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_execution_plan)

add_library(ahfl_backend_execution_journal STATIC
    execution_journal.cpp
)

target_link_libraries(ahfl_backend_execution_journal
    PUBLIC
        ahfl_execution_journal
)

ahfl_apply_project_warnings(ahfl_backend_execution_journal)

add_library(ahfl_backend_replay_view STATIC
    replay_view.cpp
)

target_link_libraries(ahfl_backend_replay_view
    PUBLIC
        ahfl_replay_view
)

ahfl_apply_project_warnings(ahfl_backend_replay_view)

add_library(ahfl_backend_audit_report STATIC
    audit_report.cpp
)

target_link_libraries(ahfl_backend_audit_report
    PUBLIC
        ahfl_audit_report
)

ahfl_apply_project_warnings(ahfl_backend_audit_report)

add_library(ahfl_backend_scheduler_snapshot STATIC
    scheduler_snapshot.cpp
)

target_link_libraries(ahfl_backend_scheduler_snapshot
    PUBLIC
        ahfl_scheduler_snapshot
)

ahfl_apply_project_warnings(ahfl_backend_scheduler_snapshot)

add_library(ahfl_backend_checkpoint_record STATIC
    checkpoint_record.cpp
)

target_link_libraries(ahfl_backend_checkpoint_record
    PUBLIC
        ahfl_checkpoint_record
)

ahfl_apply_project_warnings(ahfl_backend_checkpoint_record)

add_library(ahfl_backend_checkpoint_review STATIC
    checkpoint_review.cpp
)

target_link_libraries(ahfl_backend_checkpoint_review
    PUBLIC
        ahfl_checkpoint_record
)

ahfl_apply_project_warnings(ahfl_backend_checkpoint_review)

add_library(ahfl_backend_persistence_descriptor STATIC
    persistence_descriptor.cpp
)

target_link_libraries(ahfl_backend_persistence_descriptor
    PUBLIC
        ahfl_persistence_descriptor
)

ahfl_apply_project_warnings(ahfl_backend_persistence_descriptor)

add_library(ahfl_backend_persistence_review STATIC
    persistence_review.cpp
)

target_link_libraries(ahfl_backend_persistence_review
    PUBLIC
        ahfl_persistence_descriptor
)

ahfl_apply_project_warnings(ahfl_backend_persistence_review)

add_library(ahfl_backend_persistence_export_manifest STATIC
    persistence_export_manifest.cpp
)

target_link_libraries(ahfl_backend_persistence_export_manifest
    PUBLIC
        ahfl_persistence_export
)

ahfl_apply_project_warnings(ahfl_backend_persistence_export_manifest)

add_library(ahfl_backend_persistence_export_review STATIC
    persistence_export_review.cpp
)

target_link_libraries(ahfl_backend_persistence_export_review
    PUBLIC
        ahfl_persistence_export
)

ahfl_apply_project_warnings(ahfl_backend_persistence_export_review)

add_library(ahfl_backend_store_import_descriptor STATIC
    store_import_descriptor.cpp
)

target_link_libraries(ahfl_backend_store_import_descriptor
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_backend_store_import_descriptor)

add_library(ahfl_backend_store_import_review STATIC
    store_import_review.cpp
)

target_link_libraries(ahfl_backend_store_import_review
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_backend_store_import_review)

add_library(ahfl_backend_durable_store_import_request STATIC
    durable_store_import_request.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_request
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_request)

add_library(ahfl_backend_durable_store_import_review STATIC
    durable_store_import_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_review)

add_library(ahfl_backend_durable_store_import_decision STATIC
    durable_store_import_decision.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_decision
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_decision)

add_library(ahfl_backend_durable_store_import_receipt STATIC
    durable_store_import_receipt.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt)

add_library(ahfl_backend_durable_store_import_receipt_persistence_request STATIC
    durable_store_import_receipt_persistence_request.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt_persistence_request
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt_persistence_request)

add_library(ahfl_backend_durable_store_import_decision_review STATIC
    durable_store_import_decision_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_decision_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_decision_review)

add_library(ahfl_backend_durable_store_import_receipt_review STATIC
    durable_store_import_receipt_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt_review)

add_library(ahfl_backend_durable_store_import_receipt_persistence_review STATIC
    durable_store_import_receipt_persistence_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt_persistence_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt_persistence_review)

add_library(ahfl_backend_durable_store_import_receipt_persistence_response STATIC
    durable_store_import_receipt_persistence_response.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt_persistence_response
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt_persistence_response)

add_library(ahfl_backend_durable_store_import_receipt_persistence_response_review STATIC
    durable_store_import_receipt_persistence_response_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_receipt_persistence_response_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_receipt_persistence_response_review)

add_library(ahfl_backend_durable_store_import_adapter_execution STATIC
    durable_store_import_adapter_execution.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_adapter_execution
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_adapter_execution)

add_library(ahfl_backend_durable_store_import_recovery_preview STATIC
    durable_store_import_recovery_preview.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_recovery_preview
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_recovery_preview)

add_library(ahfl_backend_durable_store_import_provider_write_attempt STATIC
    durable_store_import_provider_write_attempt.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_write_attempt
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_write_attempt)

add_library(ahfl_backend_durable_store_import_provider_recovery_handoff STATIC
    durable_store_import_provider_recovery_handoff.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_recovery_handoff
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_recovery_handoff)

add_library(ahfl_backend_durable_store_import_provider_driver_binding STATIC
    durable_store_import_provider_driver_binding.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_driver_binding
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_driver_binding)

add_library(ahfl_backend_durable_store_import_provider_driver_readiness STATIC
    durable_store_import_provider_driver_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_driver_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_driver_readiness)

add_library(ahfl_backend_durable_store_import_provider_runtime_preflight STATIC
    durable_store_import_provider_runtime_preflight.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_runtime_preflight
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_runtime_preflight)

add_library(ahfl_backend_durable_store_import_provider_runtime_readiness STATIC
    durable_store_import_provider_runtime_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_runtime_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_runtime_readiness)

add_library(ahfl_backend_durable_store_import_provider_sdk_envelope STATIC
    durable_store_import_provider_sdk_envelope.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_envelope
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_envelope)

add_library(ahfl_backend_durable_store_import_provider_sdk_handoff_readiness STATIC
    durable_store_import_provider_sdk_handoff_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_handoff_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_handoff_readiness)

add_library(ahfl_backend_durable_store_import_provider_host_execution STATIC
    durable_store_import_provider_host_execution.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_host_execution
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_host_execution)

add_library(ahfl_backend_durable_store_import_provider_host_execution_readiness STATIC
    durable_store_import_provider_host_execution_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_host_execution_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_host_execution_readiness)

add_library(ahfl_backend_durable_store_import_provider_local_host_execution_receipt STATIC
    durable_store_import_provider_local_host_execution_receipt.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_local_host_execution_receipt
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_local_host_execution_receipt)

add_library(ahfl_backend_durable_store_import_provider_local_host_execution_receipt_review STATIC
    durable_store_import_provider_local_host_execution_receipt_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_local_host_execution_receipt_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_local_host_execution_receipt_review)

add_library(ahfl_backend_durable_store_import_provider_sdk_adapter_request STATIC
    durable_store_import_provider_sdk_adapter_request.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_adapter_request
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_adapter_request)

add_library(ahfl_backend_durable_store_import_provider_sdk_adapter_response_placeholder STATIC
    durable_store_import_provider_sdk_adapter_response_placeholder.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_adapter_response_placeholder
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_sdk_adapter_response_placeholder)

add_library(ahfl_backend_durable_store_import_provider_sdk_adapter_readiness STATIC
    durable_store_import_provider_sdk_adapter_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_adapter_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_adapter_readiness)

add_library(ahfl_backend_durable_store_import_provider_sdk_adapter_interface STATIC
    durable_store_import_provider_sdk_adapter_interface.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_adapter_interface
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_adapter_interface)

add_library(ahfl_backend_durable_store_import_provider_sdk_adapter_interface_review STATIC
    durable_store_import_provider_sdk_adapter_interface_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_adapter_interface_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_sdk_adapter_interface_review)

add_library(ahfl_backend_durable_store_import_provider_config_load STATIC
    durable_store_import_provider_config_load.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_config_load
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_config_load)

add_library(ahfl_backend_durable_store_import_provider_config_snapshot STATIC
    durable_store_import_provider_config_snapshot.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_config_snapshot
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_config_snapshot)

add_library(ahfl_backend_durable_store_import_provider_config_readiness STATIC
    durable_store_import_provider_config_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_config_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_config_readiness)

add_library(ahfl_backend_durable_store_import_provider_secret_resolver_request STATIC
    durable_store_import_provider_secret_resolver_request.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_secret_resolver_request
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_secret_resolver_request)

add_library(ahfl_backend_durable_store_import_provider_secret_resolver_response STATIC
    durable_store_import_provider_secret_resolver_response.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_secret_resolver_response
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_secret_resolver_response)

add_library(ahfl_backend_durable_store_import_provider_secret_policy_review STATIC
    durable_store_import_provider_secret_policy_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_secret_policy_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_secret_policy_review)

add_library(ahfl_backend_durable_store_import_provider_local_host_harness_request STATIC
    durable_store_import_provider_local_host_harness_request.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_local_host_harness_request
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_local_host_harness_request)

add_library(ahfl_backend_durable_store_import_provider_local_host_harness_record STATIC
    durable_store_import_provider_local_host_harness_record.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_local_host_harness_record
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_local_host_harness_record)

add_library(ahfl_backend_durable_store_import_provider_local_host_harness_review STATIC
    durable_store_import_provider_local_host_harness_review.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_local_host_harness_review
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_local_host_harness_review)

add_library(ahfl_backend_durable_store_import_provider_sdk_payload_plan STATIC
    durable_store_import_provider_sdk_payload_plan.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_payload_plan
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_payload_plan)

add_library(ahfl_backend_durable_store_import_provider_sdk_payload_audit STATIC
    durable_store_import_provider_sdk_payload_audit.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_payload_audit
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(ahfl_backend_durable_store_import_provider_sdk_payload_audit)

add_library(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_contract STATIC
    durable_store_import_provider_sdk_mock_adapter_contract.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_contract
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_sdk_mock_adapter_contract)

add_library(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_execution STATIC
    durable_store_import_provider_sdk_mock_adapter_execution.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_execution
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_sdk_mock_adapter_execution)

add_library(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_readiness STATIC
    durable_store_import_provider_sdk_mock_adapter_readiness.cpp
)

target_link_libraries(ahfl_backend_durable_store_import_provider_sdk_mock_adapter_readiness
    PUBLIC
        ahfl_durable_store_import
)

ahfl_apply_project_warnings(
    ahfl_backend_durable_store_import_provider_sdk_mock_adapter_readiness)

add_library(ahfl_backend_scheduler_review STATIC
    scheduler_review.cpp
)

target_link_libraries(ahfl_backend_scheduler_review
    PUBLIC
        ahfl_scheduler_snapshot
)

ahfl_apply_project_warnings(ahfl_backend_scheduler_review)

add_library(ahfl_backend_dry_run_trace STATIC
    dry_run_trace.cpp
)

target_link_libraries(ahfl_backend_dry_run_trace
    PUBLIC
        ahfl_dry_run
)

ahfl_apply_project_warnings(ahfl_backend_dry_run_trace)

add_library(ahfl_backend_runtime_session STATIC
    runtime_session.cpp
)

target_link_libraries(ahfl_backend_runtime_session
    PUBLIC
        ahfl_runtime_session
)

ahfl_apply_project_warnings(ahfl_backend_runtime_session)

add_library(ahfl_backend_package_review STATIC
    package_review.cpp
)

target_link_libraries(ahfl_backend_package_review
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_package_review)

add_library(ahfl_backend STATIC
    driver.cpp
)

target_link_libraries(ahfl_backend
    PUBLIC
        ahfl_ir
        ahfl_backend_audit_report
        ahfl_backend_scheduler_review
        ahfl_backend_dry_run_trace
        ahfl_backend_execution_plan
        ahfl_backend_execution_journal
        ahfl_backend_replay_view
        ahfl_backend_scheduler_snapshot
        ahfl_backend_checkpoint_record
        ahfl_backend_checkpoint_review
        ahfl_backend_persistence_descriptor
        ahfl_backend_persistence_review
        ahfl_backend_persistence_export_manifest
        ahfl_backend_persistence_export_review
        ahfl_backend_store_import_descriptor
        ahfl_backend_store_import_review
        ahfl_backend_durable_store_import_request
        ahfl_backend_durable_store_import_review
        ahfl_backend_durable_store_import_decision
        ahfl_backend_durable_store_import_receipt
        ahfl_backend_durable_store_import_receipt_persistence_request
        ahfl_backend_durable_store_import_decision_review
        ahfl_backend_durable_store_import_receipt_review
        ahfl_backend_durable_store_import_receipt_persistence_review
        ahfl_backend_durable_store_import_receipt_persistence_response
        ahfl_backend_durable_store_import_receipt_persistence_response_review
        ahfl_backend_durable_store_import_adapter_execution
        ahfl_backend_durable_store_import_recovery_preview
        ahfl_backend_durable_store_import_provider_write_attempt
        ahfl_backend_durable_store_import_provider_recovery_handoff
        ahfl_backend_durable_store_import_provider_driver_binding
        ahfl_backend_durable_store_import_provider_driver_readiness
        ahfl_backend_durable_store_import_provider_runtime_preflight
        ahfl_backend_durable_store_import_provider_runtime_readiness
        ahfl_backend_durable_store_import_provider_sdk_envelope
        ahfl_backend_durable_store_import_provider_sdk_handoff_readiness
        ahfl_backend_durable_store_import_provider_host_execution
        ahfl_backend_durable_store_import_provider_host_execution_readiness
        ahfl_backend_durable_store_import_provider_local_host_execution_receipt
        ahfl_backend_durable_store_import_provider_local_host_execution_receipt_review
        ahfl_backend_durable_store_import_provider_sdk_adapter_request
        ahfl_backend_durable_store_import_provider_sdk_adapter_response_placeholder
        ahfl_backend_durable_store_import_provider_sdk_adapter_readiness
        ahfl_backend_durable_store_import_provider_sdk_adapter_interface
        ahfl_backend_durable_store_import_provider_sdk_adapter_interface_review
        ahfl_backend_durable_store_import_provider_config_load
        ahfl_backend_durable_store_import_provider_config_snapshot
        ahfl_backend_durable_store_import_provider_config_readiness
        ahfl_backend_durable_store_import_provider_secret_resolver_request
        ahfl_backend_durable_store_import_provider_secret_resolver_response
        ahfl_backend_durable_store_import_provider_secret_policy_review
        ahfl_backend_durable_store_import_provider_local_host_harness_request
        ahfl_backend_durable_store_import_provider_local_host_harness_record
        ahfl_backend_durable_store_import_provider_local_host_harness_review
        ahfl_backend_durable_store_import_provider_sdk_payload_plan
        ahfl_backend_durable_store_import_provider_sdk_payload_audit
        ahfl_backend_durable_store_import_provider_sdk_mock_adapter_contract
        ahfl_backend_durable_store_import_provider_sdk_mock_adapter_execution
        ahfl_backend_durable_store_import_provider_sdk_mock_adapter_readiness
        ahfl_backend_native_json
        ahfl_backend_package_review
        ahfl_backend_runtime_session
        ahfl_backend_summary
        ahfl_backend_smv
)

ahfl_apply_project_warnings(ahfl_backend)

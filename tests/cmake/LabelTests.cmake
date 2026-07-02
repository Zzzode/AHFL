ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-model
    TESTS
        ahflc.dump_project.search_root.ok_cross_file
        ahflc.check.manifest_requires_toml_extension
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-debug
    TESTS
        ahflc.dump_project.search_root.ok_cross_file
        ahflc.dump_ast.search_root.ok_cross_file
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-semantics
    TESTS
        ahflc.check.project.ok_cross_file
        ahflc.check.project.fail_node_input
        ahflc.check.manifest_basic
        ahflc.check.workspace_basic
        ahfl.check.project.ok_expression_type_isolated
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-ir
    TESTS
        ahflc.emit_ir.workflow_value_flow
        ahflc.emit_ir_json.workflow_value_flow
        ahflc.emit_ir_json.search_root.workflow_value_flow
        ahfl.check.project.ok_expression_type_isolated
        ahfl.handoff.package_compat.escape_control_characters
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-backend
    TESTS
        ahflc.emit_summary.workflow_value_flow
        ahflc.emit_summary.search_root.workflow_value_flow
        ahflc.emit_smv.project.ok_cross_file
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-compat
    TESTS
        ahflc.check.project.ok_cross_file
        ahflc.emit_ir.project.ok_cross_file
        ahflc.emit_ir_json.project.ok_cross_file
        ahflc.emit_smv.project.ok_cross_file
)

ahfl_label_tests(
    LABELS ahfl-v0.4 v0.4-package-model
    TESTS
        ahfl.handoff.package.project_workflow_value_flow
        ahfl.handoff.package.file_expr_temporal
)

ahfl_label_tests(
    LABELS ahfl-v0.4 v0.4-package-emission
    TESTS
        ahflc.emit_native_json.expr_temporal
        ahflc.emit_native_json.workflow_value_flow
        ahflc.emit_native_json.project.workflow_value_flow
        ahflc.emit_native_json.manifest_basic
        ahflc.emit_native_json.workspace_basic
)

ahfl_label_tests(
    LABELS ahfl-v0.4 v0.4-package-compat
    TESTS
        ahfl.handoff.package_compat.normalize_identity_format_version
        ahfl.handoff.package_compat.omit_empty_provenance
        ahfl.handoff.package_compat.escape_control_characters
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-authoring-emission
    TESTS
        ahflc.emit_native_json.package_requires_workspace
        ahflc.emit_native_json.manifest_rejects_workspace_package_selector
        ahflc.emit_package_review.manifest_basic
        ahflc.emit_package_review.workspace_basic
        ahflc.emit_execution_plan.manifest_basic
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-authoring-validation
    TESTS
        ahfl.handoff.package.validate_normalizes_display_names
        ahfl.handoff.package.validate_rejects_wrong_kind
        ahfl.handoff.package.validate_rejects_duplicate_normalized_targets
        ahfl.handoff.package.validate_rejects_unknown_capability
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-review
    TESTS
        ahflc.emit_package_review.workflow_value_flow.with_package
        ahflc.emit_package_review.manifest_basic
        ahflc.emit_package_review.manifest.workflow_value_flow.with_package
        ahflc.emit_package_review.workspace_basic
        ahflc.emit_package_review.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-reference-consumer
    TESTS
        ahfl.handoff.package.package_reader_summary.project_workflow_value_flow
        ahfl.handoff.package.package_reader_summary.fail_missing_export
        ahfl.handoff.package.execution_planner_bootstrap.project_workflow_value_flow
        ahfl.handoff.package.execution_planner_bootstrap.fail_agent_entry
        ahfl.handoff.package.execution_planner_bootstrap.fail_missing_dependency
        ahflc.emit_package_review.workflow_value_flow.with_package
        ahflc.emit_package_review.manifest_basic
        ahflc.emit_package_review.manifest.workflow_value_flow.with_package
        ahflc.emit_package_review.workspace_basic
        ahflc.emit_package_review.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-execution-plan-model
    TESTS
        ahfl.handoff.package.execution_plan.project_workflow_value_flow
        ahfl.handoff.package.execution_plan.fail_agent_entry
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-execution-plan-emission
    TESTS
        ahflc.emit_execution_plan.workflow_value_flow.with_package
        ahflc.emit_execution_plan.manifest_basic
        ahflc.emit_execution_plan.manifest.workflow_value_flow.with_package
        ahflc.emit_execution_plan.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-execution-plan-validation
    TESTS
        ahfl.handoff.package.execution_plan.validate_project_workflow_value_flow
        ahfl.handoff.package.execution_plan.validate_fail_missing_entry_workflow
        ahfl.handoff.package.execution_plan.validate_fail_unknown_value_read
        ahflc.emit_execution_plan.manifest.workflow_value_flow.fail_agent_entry
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-dry-run-model
    TESTS
        ahfl.dry_run.local.project_workflow_value_flow
        ahfl.dry_run.local.fail_missing_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-dry-run-mock-input
    TESTS
        ahfl.dry_run.mock_set.parse_ok
        ahfl.dry_run.mock_set.parse_fail_duplicate_selector
        ahfl.dry_run.mock_set.parse_fail_duplicate_json_field
        ahfl.dry_run.local.fail_missing_mock
        ahfl.dry_run.local.fail_unused_mock
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-dry-run-trace
    TESTS
        ahflc.emit_dry_run_trace.workflow_value_flow.with_package
        ahflc.emit_dry_run_trace.manifest_requires_capability_mocks
        ahflc.emit_dry_run_trace.manifest.workflow_value_flow.with_package
        ahflc.emit_dry_run_trace.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-runtime-session-model
    TESTS
        ahfl.runtime_session.model.project_workflow_value_flow
        ahfl.runtime_session.model.fail_missing_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-runtime-session-bootstrap
    TESTS
        ahfl.runtime_session.model.fail_missing_mock
        ahfl.runtime_session.model.partial_pending_mock
        ahfl.runtime_session.model.fail_node_failure
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-runtime-session-validation
    TESTS
        ahfl.runtime_session.validation.project_workflow_value_flow
        ahfl.runtime_session.validation.fail_incomplete_completed_workflow
        ahfl.runtime_session.validation.fail_missing_used_mock
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-runtime-session-validation
    TESTS
        ahfl.runtime_session.validation.partial_workflow_ok
        ahfl.runtime_session.validation.failed_workflow_ok
        ahfl.runtime_session.validation.fail_failed_without_failure_summary
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-runtime-session-emission
    TESTS
        ahflc.emit_runtime_session.workflow_value_flow.with_package
        ahflc.emit_runtime_session.manifest.workflow_value_flow.with_package
        ahflc.emit_runtime_session.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-runtime-session-emission
    TESTS
        ahflc.emit_runtime_session.alias_const.partial.with_package
        ahflc.emit_runtime_session.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_runtime_session.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-execution-journal-model
    TESTS
        ahfl.execution_journal.model.validate_ok
        ahfl.execution_journal.model.fail_out_of_order_node_phase
        ahfl.execution_journal.model.fail_non_monotonic_execution_index
        ahfl.execution_journal.model.fail_events_after_workflow_completed
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-execution-journal-model
    TESTS
        ahfl.execution_journal.model.failure_path_ok
        ahfl.execution_journal.model.fail_failed_before_node_started
        ahfl.execution_journal.model.fail_failed_without_workflow_failed
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-execution-journal-bootstrap
    TESTS
        ahfl.execution_journal.bootstrap.from_runtime_session_ok
        ahfl.execution_journal.bootstrap.fail_invalid_runtime_session
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-execution-journal-bootstrap
    TESTS
        ahfl.execution_journal.bootstrap.from_failed_runtime_session_ok
        ahfl.execution_journal.bootstrap.from_partial_runtime_session_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.7 v0.7-execution-journal-emission
    TESTS
        ahflc.emit_execution_journal.workflow_value_flow.with_package
        ahflc.emit_execution_journal.manifest.workflow_value_flow.with_package
        ahflc.emit_execution_journal.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-replay-view-model
    TESTS
        ahfl.replay_view.model.project_workflow_value_flow
        ahfl.replay_view.model.fail_invalid_execution_journal
        ahfl.replay_view.model.fail_session_journal_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-replay-view-model
    TESTS
        ahfl.replay_view.model.failed_workflow
        ahfl.replay_view.model.partial_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-replay-view-validation
    TESTS
        ahfl.replay_view.validation.project_workflow_value_flow
        ahfl.replay_view.validation.fail_missing_completed_progression
        ahfl.replay_view.validation.fail_execution_order_index_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-replay-view-emission
    TESTS
        ahflc.emit_replay_view.workflow_value_flow.with_package
        ahflc.emit_replay_view.manifest.workflow_value_flow.with_package
        ahflc.emit_replay_view.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-replay-view-emission
    TESTS
        ahflc.emit_replay_view.alias_const.partial.with_package
        ahflc.emit_replay_view.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_replay_view.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-audit-report-model
    TESTS
        ahfl.audit_report.model.validate_ok
        ahfl.audit_report.model.fail_unknown_session_node
        ahfl.audit_report.model.fail_journal_order_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-audit-report-bootstrap
    TESTS
        ahfl.audit_report.bootstrap.project_workflow_value_flow
        ahfl.audit_report.bootstrap.fail_trace_workflow_mismatch
        ahfl.audit_report.bootstrap.fail_trace_execution_order_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-audit-report-bootstrap
    TESTS
        ahfl.audit_report.bootstrap.failed_workflow
        ahfl.audit_report.bootstrap.partial_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.8 v0.8-audit-report-emission
    TESTS
        ahflc.emit_audit_report.workflow_value_flow.with_package
        ahflc.emit_audit_report.manifest.workflow_value_flow.with_package
        ahflc.emit_audit_report.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-audit-report-emission
    TESTS
        ahflc.emit_audit_report.alias_const.partial.with_package
        ahflc.emit_audit_report.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_audit_report.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-snapshot-model
    TESTS
        ahfl.scheduler_snapshot.model.validate_ok
        ahfl.scheduler_snapshot.model.validate_failed_ok
        ahfl.scheduler_snapshot.model.fail_runnable_without_ready_nodes
        ahfl.scheduler_snapshot.model.fail_next_candidate_not_ready
        ahfl.scheduler_snapshot.model.fail_non_prefix_cursor
        ahfl.scheduler_snapshot.model.fail_ready_dependency_outside_planned_set
        ahfl.scheduler_snapshot.model.fail_unknown_ready_node
        ahfl.scheduler_snapshot.model.fail_blocked_terminal_failure_without_summary
        ahfl.scheduler_snapshot.model.fail_terminal_completed_without_full_prefix
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-snapshot-bootstrap
    TESTS
        ahfl.scheduler_snapshot.bootstrap.project_workflow_value_flow
        ahfl.scheduler_snapshot.bootstrap.failed_workflow
        ahfl.scheduler_snapshot.bootstrap.partial_workflow
        ahfl.scheduler_snapshot.bootstrap.fail_replay_workflow_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-review-model
    TESTS
        ahfl.scheduler_review.model.completed_summary
        ahfl.scheduler_review.model.failed_summary
        ahfl.scheduler_review.model.partial_summary
        ahfl.scheduler_review.model.fail_invalid_snapshot
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-compatibility
    TESTS
        ahfl.scheduler_snapshot.compat.fail_unsupported_format_version
        ahfl.scheduler_snapshot.compat.fail_unsupported_source_replay_view_format_version
        ahfl.scheduler_review.compat.validate_ok
        ahfl.scheduler_review.compat.fail_unsupported_format_version
        ahfl.scheduler_review.compat.fail_unsupported_source_snapshot_format_version
        ahfl.scheduler_review.compat.fail_prefix_size_mismatch
        ahfl.scheduler_review.compat.fail_runnable_terminal_reason
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-snapshot-emission
    TESTS
        ahflc.emit_scheduler_snapshot.workflow_value_flow.with_package
        ahflc.emit_scheduler_snapshot.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-review-emission
    TESTS
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-golden
    TESTS
        ahflc.emit_scheduler_snapshot.workflow_value_flow.with_package
        ahflc.emit_scheduler_snapshot.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-regression
    TESTS
        ahfl.scheduler_snapshot.model.validate_ok
        ahfl.scheduler_snapshot.model.validate_failed_ok
        ahfl.scheduler_snapshot.model.fail_runnable_without_ready_nodes
        ahfl.scheduler_snapshot.model.fail_next_candidate_not_ready
        ahfl.scheduler_snapshot.model.fail_non_prefix_cursor
        ahfl.scheduler_snapshot.model.fail_ready_dependency_outside_planned_set
        ahfl.scheduler_snapshot.model.fail_unknown_ready_node
        ahfl.scheduler_snapshot.model.fail_blocked_terminal_failure_without_summary
        ahfl.scheduler_snapshot.model.fail_terminal_completed_without_full_prefix
        ahfl.scheduler_snapshot.compat.fail_unsupported_format_version
        ahfl.scheduler_snapshot.compat.fail_unsupported_source_replay_view_format_version
        ahfl.scheduler_snapshot.bootstrap.project_workflow_value_flow
        ahfl.scheduler_snapshot.bootstrap.failed_workflow
        ahfl.scheduler_snapshot.bootstrap.partial_workflow
        ahfl.scheduler_snapshot.bootstrap.fail_replay_workflow_mismatch
        ahfl.scheduler_review.model.completed_summary
        ahfl.scheduler_review.model.failed_summary
        ahfl.scheduler_review.model.partial_summary
        ahfl.scheduler_review.model.fail_invalid_snapshot
        ahfl.scheduler_review.compat.validate_ok
        ahfl.scheduler_review.compat.fail_unsupported_format_version
        ahfl.scheduler_review.compat.fail_unsupported_source_snapshot_format_version
        ahfl.scheduler_review.compat.fail_prefix_size_mismatch
        ahfl.scheduler_review.compat.fail_runnable_terminal_reason
        ahflc.emit_scheduler_snapshot.workflow_value_flow.with_package
        ahflc.emit_scheduler_snapshot.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-record-model
    TESTS
        ahfl.checkpoint_record.model.validate_ok
        ahfl.checkpoint_record.model.validate_blocked_ok
        ahfl.checkpoint_record.model.validate_terminal_completed_ok
        ahfl.checkpoint_record.model.validate_terminal_failed_ok
        ahfl.checkpoint_record.model.fail_non_prefix_cursor
        ahfl.checkpoint_record.model.fail_resume_ready_with_blocker
        ahfl.checkpoint_record.model.fail_terminal_completed_without_full_prefix
        ahfl.checkpoint_record.model.fail_terminal_failed_without_failure_summary
        ahfl.checkpoint_record.model.fail_durable_adjacent_without_checkpoint_friendly_source
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-record-bootstrap
    TESTS
        ahfl.checkpoint_record.bootstrap.project_workflow_value_flow
        ahfl.checkpoint_record.bootstrap.failed_workflow
        ahfl.checkpoint_record.bootstrap.partial_workflow
        ahfl.checkpoint_record.bootstrap.fail_snapshot_workflow_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-record-emission
    TESTS
        ahflc.emit_checkpoint_record.workflow_value_flow.with_package
        ahflc.emit_checkpoint_record.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_record.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-review-model
    TESTS
        ahfl.checkpoint_review.model.completed_summary
        ahfl.checkpoint_review.model.failed_summary
        ahfl.checkpoint_review.model.partial_summary
        ahfl.checkpoint_review.model.fail_invalid_record
        ahfl.checkpoint_review.compat.validate_ok
        ahfl.checkpoint_review.compat.fail_unsupported_format_version
        ahfl.checkpoint_review.compat.fail_unsupported_source_record_format_version
        ahfl.checkpoint_review.compat.fail_prefix_size_mismatch
        ahfl.checkpoint_review.compat.fail_ready_terminal_reason
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-review-emission
    TESTS
        ahflc.emit_checkpoint_review.workflow_value_flow.with_package
        ahflc.emit_checkpoint_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-golden
    TESTS
        ahflc.emit_checkpoint_record.workflow_value_flow.with_package
        ahflc.emit_checkpoint_record.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_record.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_checkpoint_review.workflow_value_flow.with_package
        ahflc.emit_checkpoint_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-descriptor-model
    TESTS
        ahfl.persistence_descriptor.model.validate_ok
        ahfl.persistence_descriptor.model.validate_blocked_ok
        ahfl.persistence_descriptor.model.validate_terminal_failed_ok
        ahfl.persistence_descriptor.model.validate_terminal_partial_ok
        ahfl.persistence_descriptor.model.fail_missing_planned_identity
        ahfl.persistence_descriptor.model.fail_non_prefix_cursor
        ahfl.persistence_descriptor.model.fail_export_ready_with_blocker
        ahfl.persistence_descriptor.model.fail_unsupported_checkpoint_record_format
        ahfl.persistence_descriptor.model.fail_ready_from_blocked_checkpoint
        ahfl.persistence_descriptor.model.fail_terminal_failed_export_ready
        ahfl.persistence_descriptor.model.fail_store_adjacent_blocked
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-descriptor-bootstrap
    TESTS
        ahfl.persistence_descriptor.bootstrap.project_workflow_value_flow
        ahfl.persistence_descriptor.bootstrap.failed_workflow
        ahfl.persistence_descriptor.bootstrap.partial_workflow
        ahfl.persistence_descriptor.bootstrap.fail_invalid_checkpoint_record
        ahfl.persistence_descriptor.bootstrap.fail_checkpoint_workflow_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-descriptor-emission
    TESTS
        ahflc.emit_persistence_descriptor.workflow_value_flow.with_package
        ahflc.emit_persistence_descriptor.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_descriptor.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-review-model
    TESTS
        ahfl.persistence_review.model.validate_ok
        ahfl.persistence_review.model.fail_unsupported_source_descriptor_format
        ahfl.persistence_review.model.fail_invalid_descriptor
        ahfl.persistence_review.model.project_workflow_value_flow
        ahfl.persistence_review.model.failed_workflow
        ahfl.persistence_review.model.partial_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-persistence-export-manifest-model
    TESTS
        ahfl.persistence_export_manifest.model.validate_ok
        ahfl.persistence_export_manifest.model.validate_blocked_ok
        ahfl.persistence_export_manifest.model.validate_terminal_failed_ok
        ahfl.persistence_export_manifest.model.validate_terminal_partial_ok
        ahfl.persistence_export_manifest.model.fail_missing_export_package_identity
        ahfl.persistence_export_manifest.model.fail_duplicate_artifact_name
        ahfl.persistence_export_manifest.model.fail_ready_with_blocker
        ahfl.persistence_export_manifest.model.fail_ready_from_blocked_persistence
        ahfl.persistence_export_manifest.model.fail_unsupported_source_descriptor_format
        ahfl.persistence_export_manifest.model.fail_store_import_adjacent_blocked
        ahfl.persistence_export_manifest.model.fail_terminal_failed_without_failure_summary
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-persistence-export-manifest-bootstrap
    TESTS
        ahfl.persistence_export_manifest.bootstrap.project_workflow_value_flow
        ahfl.persistence_export_manifest.bootstrap.failed_workflow
        ahfl.persistence_export_manifest.bootstrap.partial_workflow
        ahfl.persistence_export_manifest.bootstrap.fail_invalid_descriptor
        ahfl.persistence_export_manifest.bootstrap.fail_descriptor_workflow_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-persistence-export-manifest-emission
    TESTS
        ahflc.emit_export_manifest.workflow_value_flow.with_package
        ahflc.emit_export_manifest.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_manifest.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-persistence-export-review-model
    TESTS
        ahfl.persistence_export_review.model.validate_ok
        ahfl.persistence_export_review.model.fail_unsupported_source_manifest_format
        ahfl.persistence_export_review.model.fail_invalid_manifest
        ahfl.persistence_export_review.model.project_workflow_value_flow
        ahfl.persistence_export_review.model.failed_workflow
        ahfl.persistence_export_review.model.partial_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-persistence-export-review-emission
    TESTS
        ahflc.emit_export_review.workflow_value_flow.with_package
        ahflc.emit_export_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-export-golden
    TESTS
        ahflc.emit_export_manifest.workflow_value_flow.with_package
        ahflc.emit_export_manifest.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_manifest.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_export_review.workflow_value_flow.with_package
        ahflc.emit_export_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-descriptor-model
    TESTS
        ahfl.store_import_descriptor.model.validate_ok
        ahfl.store_import_descriptor.model.validate_blocked_ok
        ahfl.store_import_descriptor.model.validate_terminal_failed_ok
        ahfl.store_import_descriptor.model.validate_terminal_partial_ok
        ahfl.store_import_descriptor.model.fail_missing_candidate_identity
        ahfl.store_import_descriptor.model.fail_duplicate_artifact_name
        ahfl.store_import_descriptor.model.fail_ready_with_blocker
        ahfl.store_import_descriptor.model.fail_ready_from_blocked_manifest
        ahfl.store_import_descriptor.model.fail_unsupported_source_manifest_format
        ahfl.store_import_descriptor.model.fail_adapter_consumable_blocked
        ahfl.store_import_descriptor.model.fail_terminal_failed_without_failure_summary
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-descriptor-bootstrap
    TESTS
        ahfl.store_import_descriptor.bootstrap.project_workflow_value_flow
        ahfl.store_import_descriptor.bootstrap.failed_workflow
        ahfl.store_import_descriptor.bootstrap.partial_workflow
        ahfl.store_import_descriptor.bootstrap.fail_invalid_manifest
        ahfl.store_import_descriptor.bootstrap.fail_manifest_workflow_mismatch
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-review-model
    TESTS
        ahfl.store_import_review.model.validate_ok
        ahfl.store_import_review.model.fail_unsupported_source_descriptor_format
        ahfl.store_import_review.model.project_workflow_value_flow
        ahfl.store_import_review.model.failed_workflow
        ahfl.store_import_review.model.partial_workflow
        ahfl.store_import_review.model.fail_invalid_descriptor
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-emission
    TESTS
        ahflc.emit_store_import_descriptor.workflow_value_flow.with_package
        ahflc.emit_store_import_descriptor.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_store_import_review.workflow_value_flow.with_package
        ahflc.emit_store_import_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-golden
    TESTS
        ahflc.emit_store_import_descriptor.workflow_value_flow.with_package
        ahflc.emit_store_import_descriptor.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_store_import_review.workflow_value_flow.with_package
        ahflc.emit_store_import_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-request-model
    TESTS
        ahfl.durable_store_import_request.model.validate_ok
        ahfl.durable_store_import_request.model.validate_blocked_ok
        ahfl.durable_store_import_request.model.validate_terminal_failed_ok
        ahfl.durable_store_import_request.model.validate_terminal_partial_ok
        ahfl.durable_store_import_request.model.fail_missing_request_identity
        ahfl.durable_store_import_request.model.fail_duplicate_artifact_name
        ahfl.durable_store_import_request.model.fail_ready_with_blocker
        ahfl.durable_store_import_request.model.fail_unsupported_source_descriptor_format
        ahfl.durable_store_import_request.model.fail_adapter_contract_blocked
        ahfl.durable_store_import_request.model.fail_terminal_failed_without_failure_summary
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-request-bootstrap
    TESTS
        ahfl.durable_store_import_request.bootstrap.ready_descriptor
        ahfl.durable_store_import_request.bootstrap.completed_descriptor
        ahfl.durable_store_import_request.bootstrap.fail_invalid_descriptor
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-review-model
    TESTS
        ahfl.durable_store_import_review.model.validate_ok
        ahfl.durable_store_import_review.model.fail_unsupported_source_request_format
        ahfl.durable_store_import_review.model.ready_request
        ahfl.durable_store_import_review.model.failed_request
        ahfl.durable_store_import_review.model.fail_invalid_request
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-emission
    TESTS
        ahflc.emit_durable_store_import_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-golden
    TESTS
        ahflc.emit_durable_store_import_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-model
    TESTS
        ahfl.durable_store_import_decision.model.validate_ok
        ahfl.durable_store_import_decision.model.validate_blocked_ok
        ahfl.durable_store_import_decision.model.validate_deferred_ok
        ahfl.durable_store_import_decision.model.validate_rejected_ok
        ahfl.durable_store_import_decision.model.fail_missing_decision_identity
        ahfl.durable_store_import_decision.model.fail_accepted_with_blocker
        ahfl.durable_store_import_decision.model.fail_unsupported_source_request_format
        ahfl.durable_store_import_decision.model.fail_adapter_decision_consumable_blocked
        ahfl.durable_store_import_decision.model.fail_rejected_without_failure_summary
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-model
    TESTS
        ahfl.durable_store_import_receipt.model.validate_ok
        ahfl.durable_store_import_receipt.model.validate_blocked_ok
        ahfl.durable_store_import_receipt.model.validate_deferred_ok
        ahfl.durable_store_import_receipt.model.validate_rejected_ok
        ahfl.durable_store_import_receipt.model.fail_missing_receipt_identity
        ahfl.durable_store_import_receipt.model.fail_unsupported_source_decision_format
        ahfl.durable_store_import_receipt.model.fail_ready_with_blocker
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-bootstrap
    TESTS
        ahfl.durable_store_import_receipt.bootstrap.ready_decision
        ahfl.durable_store_import_receipt.bootstrap.blocked_decision
        ahfl.durable_store_import_receipt.bootstrap.deferred_decision
        ahfl.durable_store_import_receipt.bootstrap.rejected_decision
        ahfl.durable_store_import_receipt.bootstrap.fail_invalid_decision
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-review-model
    TESTS
        ahfl.durable_store_import_receipt_review.model.validate_ok
        ahfl.durable_store_import_receipt_review.model.fail_unsupported_source_receipt_format
        ahfl.durable_store_import_receipt_review.bootstrap.ready_receipt
        ahfl.durable_store_import_receipt_review.bootstrap.rejected_receipt
        ahfl.durable_store_import_receipt_review.bootstrap.fail_invalid_receipt
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-model
    TESTS
        ahfl.durable_store_import_receipt_persistence_request.model.validate_ok
        ahfl.durable_store_import_receipt_persistence_request.model.validate_blocked_ok
        ahfl.durable_store_import_receipt_persistence_request.model.validate_deferred_ok
        ahfl.durable_store_import_receipt_persistence_request.model.validate_rejected_ok
        ahfl.durable_store_import_receipt_persistence_request.model.fail_missing_request_identity
        ahfl.durable_store_import_receipt_persistence_request.model.fail_unsupported_source_receipt_format
        ahfl.durable_store_import_receipt_persistence_request.model.fail_ready_with_blocker
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-bootstrap
    TESTS
        ahfl.durable_store_import_receipt_persistence_request.bootstrap.ready_receipt
        ahfl.durable_store_import_receipt_persistence_request.bootstrap.blocked_receipt
        ahfl.durable_store_import_receipt_persistence_request.bootstrap.deferred_receipt
        ahfl.durable_store_import_receipt_persistence_request.bootstrap.rejected_receipt
        ahfl.durable_store_import_receipt_persistence_request.bootstrap.fail_invalid_receipt
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-review-model
    TESTS
        ahfl.durable_store_import_receipt_persistence_review.model.validate_ok
        ahfl.durable_store_import_receipt_persistence_review.model.fail_unsupported_source_request_format
        ahfl.durable_store_import_receipt_persistence_review.bootstrap.ready_request
        ahfl.durable_store_import_receipt_persistence_review.bootstrap.rejected_request
        ahfl.durable_store_import_receipt_persistence_review.bootstrap.fail_invalid_request
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-emission
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-golden
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-bootstrap
    TESTS
        ahfl.durable_store_import_decision.bootstrap.ready_request
        ahfl.durable_store_import_decision.bootstrap.completed_request
        ahfl.durable_store_import_decision.bootstrap.fail_invalid_request
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-review-model
    TESTS
        ahfl.durable_store_import_decision_review.model.validate_ok
        ahfl.durable_store_import_decision_review.model.fail_unsupported_source_decision_format
        ahfl.durable_store_import_decision_review.bootstrap.accepted_decision
        ahfl.durable_store_import_decision_review.bootstrap.rejected_decision
        ahfl.durable_store_import_decision_review.bootstrap.fail_invalid_decision
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-emission
    TESTS
        ahflc.emit_durable_store_import_decision.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_decision_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-golden
    TESTS
        ahflc.emit_durable_store_import_decision.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_decision_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-emission
    TESTS
        ahflc.emit_durable_store_import_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-golden
    TESTS
        ahflc.emit_durable_store_import_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-review-emission
    TESTS
        ahflc.emit_persistence_review.workflow_value_flow.with_package
        ahflc.emit_persistence_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-golden
    TESTS
        ahflc.emit_persistence_descriptor.workflow_value_flow.with_package
        ahflc.emit_persistence_descriptor.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_persistence_review.workflow_value_flow.with_package
        ahflc.emit_persistence_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-model
    TESTS
        ahfl.durable_store_import_receipt_persistence_response.model.validate_ok
        ahfl.durable_store_import_receipt_persistence_response.model.validate_blocked_ok
        ahfl.durable_store_import_receipt_persistence_response.model.validate_deferred_ok
        ahfl.durable_store_import_receipt_persistence_response.model.validate_rejected_ok
        ahfl.durable_store_import_receipt_persistence_response.model.fail_missing_response_identity
        ahfl.durable_store_import_receipt_persistence_response.model.fail_unsupported_source_request_format
        ahfl.durable_store_import_receipt_persistence_response.model.fail_accepted_with_blocker
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-bootstrap
    TESTS
        ahfl.durable_store_import_receipt_persistence_response.bootstrap.ready_request
        ahfl.durable_store_import_receipt_persistence_response.bootstrap.fail_invalid_request
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-review-model
    TESTS
        ahfl.durable_store_import_receipt_persistence_response_review.model.validate_ok
        ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.ready_response
        ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.rejected_response
        ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.fail_invalid_response
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-emission
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_response.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-golden
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_response.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.20 v0.20-durable-store-import-adapter-execution-model
    TESTS
        ahfl.durable_store_import_adapter_execution.model.validate_ok
        ahfl.durable_store_import_adapter_execution.model.validate_blocked_ok
        ahfl.durable_store_import_adapter_execution.model.validate_deferred_ok
        ahfl.durable_store_import_adapter_execution.model.validate_rejected_ok
        ahfl.durable_store_import_adapter_execution.model.fail_missing_persistence_id
        ahfl.durable_store_import_adapter_execution.model.fail_non_mutating_accepted
        ahfl.durable_store_import_adapter_execution.model.fail_mutated_non_accepted
)

ahfl_label_tests(
    LABELS ahfl-v0.20 v0.20-durable-store-import-adapter-execution-bootstrap
    TESTS
        ahfl.durable_store_import_adapter_execution.bootstrap.ready_response
        ahfl.durable_store_import_adapter_execution.bootstrap.fail_invalid_response
)

ahfl_label_tests(
    LABELS ahfl-v0.20 v0.20-durable-store-import-recovery-preview-model
    TESTS
        ahfl.durable_store_import_recovery_preview.model.validate_ok
        ahfl.durable_store_import_recovery_preview.bootstrap.ready_execution
        ahfl.durable_store_import_recovery_preview.bootstrap.rejected_execution
        ahfl.durable_store_import_recovery_preview.bootstrap.fail_invalid_execution
)

ahfl_label_tests(
    LABELS ahfl-v0.20 v0.20-durable-store-import-adapter-execution-emission
    TESTS
        ahflc.emit_durable_store_import_adapter_execution.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_adapter_execution.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_adapter_execution.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_recovery_preview.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_recovery_preview.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_recovery_preview.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.20 v0.20-durable-store-import-adapter-execution-golden
    TESTS
        ahflc.emit_durable_store_import_adapter_execution.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_adapter_execution.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_adapter_execution.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_recovery_preview.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_recovery_preview.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_recovery_preview.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-adapter-config-model
    TESTS
        ahfl.durable_store_import_provider_adapter_config.model.fail_secret_material
        ahfl.durable_store_import_provider_adapter_config.model.fail_provider_coordinates
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-write-attempt-model
    TESTS
        ahfl.durable_store_import_provider_write_attempt.model.validate_ok
        ahfl.durable_store_import_provider_write_attempt.model.validate_rejected_ok
        ahfl.durable_store_import_provider_write_attempt.model.unsupported_capability
        ahfl.durable_store_import_provider_write_attempt.model.fail_planned_without_provider_id
        ahfl.durable_store_import_provider_write_attempt.model.fail_not_planned_mutating_intent
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-write-attempt-bootstrap
    TESTS
        ahfl.durable_store_import_provider_write_attempt.bootstrap.ready_execution
        ahfl.durable_store_import_provider_write_attempt.bootstrap.fail_invalid_execution
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-recovery-handoff-model
    TESTS
        ahfl.durable_store_import_provider_recovery_handoff.model.validate_ok
        ahfl.durable_store_import_provider_recovery_handoff.bootstrap.unsupported_capability
        ahfl.durable_store_import_provider_recovery_handoff.bootstrap.fail_invalid_write_attempt
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-adapter-emission
    TESTS
        ahflc.emit_durable_store_import_provider_write_attempt.workflow_value_flow.with_package
        ahflc.emit_provider_artifact.manifest_requires_capability_mocks
        ahflc.emit_durable_store_import_provider_write_attempt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_attempt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.21 v0.21-durable-store-import-provider-adapter-golden
    TESTS
        ahflc.emit_durable_store_import_provider_write_attempt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_attempt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_attempt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_recovery_handoff.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-profile-model
    TESTS
        ahfl.durable_store_import_provider_driver_profile.model.fail_secret_material
        ahfl.durable_store_import_provider_driver_profile.model.fail_provider_coordinates
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-binding-model
    TESTS
        ahfl.durable_store_import_provider_driver_binding.model.validate_ok
        ahfl.durable_store_import_provider_driver_binding.model.validate_rejected_ok
        ahfl.durable_store_import_provider_driver_binding.model.unsupported_capability
        ahfl.durable_store_import_provider_driver_binding.model.missing_profile_load_capability
        ahfl.durable_store_import_provider_driver_binding.model.profile_mismatch
        ahfl.durable_store_import_provider_driver_binding.model.fail_sdk_invocation
        ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_descriptor
        ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_profile_load
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-binding-bootstrap
    TESTS
        ahfl.durable_store_import_provider_driver_binding.bootstrap.ready_write_attempt
        ahfl.durable_store_import_provider_driver_binding.bootstrap.fail_invalid_write_attempt
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-readiness-model
    TESTS
        ahfl.durable_store_import_provider_driver_readiness.model.validate_ok
        ahfl.durable_store_import_provider_driver_readiness.bootstrap.unsupported_capability
        ahfl.durable_store_import_provider_driver_readiness.bootstrap.fail_invalid_binding_plan
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-emission
    TESTS
        ahflc.emit_durable_store_import_provider_driver_binding.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_driver_binding.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_driver_binding.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.22 v0.22-durable-store-import-provider-driver-golden
    TESTS
        ahflc.emit_durable_store_import_provider_driver_binding.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_driver_binding.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_driver_binding.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_driver_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-profile-model
    TESTS
        ahfl.durable_store_import_provider_runtime_profile.model.fail_secret_material
        ahfl.durable_store_import_provider_runtime_profile.model.fail_provider_coordinates
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-preflight-model
    TESTS
        ahfl.durable_store_import_provider_runtime_preflight.model.validate_ok
        ahfl.durable_store_import_provider_runtime_preflight.model.validate_blocked_ok
        ahfl.durable_store_import_provider_runtime_preflight.model.unsupported_capability
        ahfl.durable_store_import_provider_runtime_preflight.model.profile_mismatch
        ahfl.durable_store_import_provider_runtime_preflight.model.fail_side_effects
        ahfl.durable_store_import_provider_runtime_preflight.model.fail_ready_without_envelope
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-preflight-bootstrap
    TESTS
        ahfl.durable_store_import_provider_runtime_preflight.bootstrap.ready_binding
        ahfl.durable_store_import_provider_runtime_preflight.bootstrap.fail_invalid_driver_binding
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-readiness-model
    TESTS
        ahfl.durable_store_import_provider_runtime_readiness.model.validate_ok
        ahfl.durable_store_import_provider_runtime_readiness.bootstrap.unsupported_capability
        ahfl.durable_store_import_provider_runtime_readiness.bootstrap.fail_invalid_preflight
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-emission
    TESTS
        ahflc.emit_durable_store_import_provider_runtime_preflight.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_runtime_preflight.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_runtime_preflight.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.23 v0.23-durable-store-import-provider-runtime-golden
    TESTS
        ahflc.emit_durable_store_import_provider_runtime_preflight.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_runtime_preflight.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_runtime_preflight.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_runtime_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-policy-model
    TESTS
        ahfl.durable_store_import_provider_sdk_policy.model.fail_secret_material
        ahfl.durable_store_import_provider_sdk_policy.model.fail_provider_coordinates
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-envelope-model
    TESTS
        ahfl.durable_store_import_provider_sdk_envelope.model.validate_ok
        ahfl.durable_store_import_provider_sdk_envelope.model.validate_blocked_ok
        ahfl.durable_store_import_provider_sdk_envelope.model.unsupported_capability
        ahfl.durable_store_import_provider_sdk_envelope.model.policy_mismatch
        ahfl.durable_store_import_provider_sdk_envelope.model.fail_side_effects
        ahfl.durable_store_import_provider_sdk_envelope.model.fail_ready_without_handoff
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-envelope-bootstrap
    TESTS
        ahfl.durable_store_import_provider_sdk_envelope.bootstrap.ready_preflight
        ahfl.durable_store_import_provider_sdk_envelope.bootstrap.fail_invalid_preflight
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-handoff-readiness-model
    TESTS
        ahfl.durable_store_import_provider_sdk_handoff_readiness.model.validate_ok
        ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.unsupported_capability
        ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.fail_invalid_envelope
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-emission
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_envelope.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_envelope.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_envelope.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.24 v0.24-durable-store-import-provider-sdk-golden
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_envelope.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_envelope.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_envelope.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-policy-model
    TESTS
        ahfl.durable_store_import_provider_host_policy.model.fail_secret_material
        ahfl.durable_store_import_provider_host_policy.model.fail_host_side_effects
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-execution-model
    TESTS
        ahfl.durable_store_import_provider_host_execution.model.validate_ok
        ahfl.durable_store_import_provider_host_execution.model.validate_blocked_ok
        ahfl.durable_store_import_provider_host_execution.model.unsupported_capability
        ahfl.durable_store_import_provider_host_execution.model.policy_mismatch
        ahfl.durable_store_import_provider_host_execution.model.fail_side_effects
        ahfl.durable_store_import_provider_host_execution.model.fail_ready_without_descriptor
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-execution-bootstrap
    TESTS
        ahfl.durable_store_import_provider_host_execution.bootstrap.ready_envelope
        ahfl.durable_store_import_provider_host_execution.bootstrap.fail_invalid_envelope
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-execution-readiness-model
    TESTS
        ahfl.durable_store_import_provider_host_execution_readiness.model.validate_ok
        ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.unsupported_capability
        ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.fail_invalid_host_execution
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-execution-emission
    TESTS
        ahflc.emit_durable_store_import_provider_host_execution.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_host_execution.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_host_execution.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.25 v0.25-durable-store-import-provider-host-execution-golden
    TESTS
        ahflc.emit_durable_store_import_provider_host_execution.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_host_execution.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_host_execution.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_host_execution_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.26 v0.26-durable-store-import-provider-local-host-execution-receipt-model
    TESTS
        ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_ok
        ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_blocked_ok
        ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_side_effects
        ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_ready_without_identity
)

ahfl_label_tests(
    LABELS ahfl-v0.26 v0.26-durable-store-import-provider-local-host-execution-receipt-bootstrap
    TESTS
        ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.ready_host_execution
        ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.fail_invalid_host_execution
)

ahfl_label_tests(
    LABELS ahfl-v0.26 v0.26-durable-store-import-provider-local-host-execution-receipt-review-model
    TESTS
        ahfl.durable_store_import_provider_local_host_execution_receipt_review.model.validate_ok
        ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.blocked_ok
        ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.fail_invalid_receipt
)

ahfl_label_tests(
    LABELS ahfl-v0.26 v0.26-durable-store-import-provider-local-host-execution-receipt-emission
    TESTS
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.26 v0.26-durable-store-import-provider-local-host-execution-receipt-golden
    TESTS
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-request-model
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_ok
        ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_blocked_ok
        ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_side_effects
        ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_forbidden_material
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-request-bootstrap
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_request.bootstrap.ready_receipt
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-response-placeholder-model
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.model.validate_ok
        ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.bootstrap.blocked_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-readiness-model
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_readiness.model.validate_ok
        ahfl.durable_store_import_provider_sdk_adapter_readiness.bootstrap.fail_invalid_placeholder
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-emission
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.27 v0.27-durable-store-import-provider-sdk-adapter-golden
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.28 v0.28-durable-store-import-provider-sdk-adapter-interface-model
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_interface.model.validate_ok
        ahfl.durable_store_import_provider_sdk_adapter_interface.model.fail_forbidden_material
        ahfl.durable_store_import_provider_sdk_adapter_interface.bootstrap.blocked_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.28 v0.28-durable-store-import-provider-sdk-adapter-interface-review-model
    TESTS
        ahfl.durable_store_import_provider_sdk_adapter_interface_review.model.validate_ok
        ahfl.durable_store_import_provider_sdk_adapter_interface_review.bootstrap.fail_invalid_plan
)

ahfl_label_tests(
    LABELS ahfl-v0.28 v0.28-durable-store-import-provider-sdk-adapter-interface-emission
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.28 v0.28-durable-store-import-provider-sdk-adapter-interface-golden
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.29 v0.29-durable-store-import-provider-config-load-model
    TESTS
        ahfl.durable_store_import_provider_config_load.model.validate_ok
        ahfl.durable_store_import_provider_config_load.model.fail_forbidden_material
        ahfl.durable_store_import_provider_config_load.bootstrap.blocked_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.29 v0.29-durable-store-import-provider-config-snapshot-model
    TESTS
        ahfl.durable_store_import_provider_config_snapshot.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.29 v0.29-durable-store-import-provider-config-readiness-model
    TESTS
        ahfl.durable_store_import_provider_config_readiness.model.validate_ok
        ahfl.durable_store_import_provider_config_readiness.bootstrap.fail_invalid_snapshot
)

ahfl_label_tests(
    LABELS ahfl-v0.29 v0.29-durable-store-import-provider-config-emission
    TESTS
        ahflc.emit_durable_store_import_provider_config_load.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_load.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_load.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.29 v0.29-durable-store-import-provider-config-golden
    TESTS
        ahflc.emit_durable_store_import_provider_config_load.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_load.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_load.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_config_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.30 v0.30-durable-store-import-provider-secret-model
    TESTS
        ahfl.durable_store_import_provider_secret_resolver_request.model.validate_ok
        ahfl.durable_store_import_provider_secret_resolver_request.model.fail_forbidden_material
        ahfl.durable_store_import_provider_secret_resolver_request.model.fail_side_effects
        ahfl.durable_store_import_provider_secret_resolver_request.bootstrap.blocked_ok
        ahfl.durable_store_import_provider_secret_resolver_response.model.validate_ok
        ahfl.durable_store_import_provider_secret_policy_review.model.validate_ok
        ahfl.durable_store_import_provider_secret_policy_review.bootstrap.fail_invalid_placeholder
)

ahfl_label_tests(
    LABELS ahfl-v0.30 v0.30-durable-store-import-provider-secret-golden
    TESTS
        ahflc.emit_durable_store_import_provider_secret_resolver_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_secret_resolver_response.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_secret_policy_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_secret_resolver_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_secret_resolver_response.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_secret_policy_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_secret_resolver_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_secret_resolver_response.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_secret_policy_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.31 v0.31-durable-store-import-provider-local-host-harness-model
    TESTS
        ahfl.durable_store_import_provider_local_host_harness_request.model.validate_ok
        ahfl.durable_store_import_provider_local_host_harness_request.model.fail_sandbox_escape
        ahfl.durable_store_import_provider_local_host_harness_record.model.validate_ok
        ahfl.durable_store_import_provider_local_host_harness_record.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_local_host_harness_review.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.31 v0.31-durable-store-import-provider-local-host-harness-golden
    TESTS
        ahflc.emit_durable_store_import_provider_local_host_harness_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_record.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_request.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_record.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_record.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_host_harness_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.32 v0.32-durable-store-import-provider-sdk-payload-model
    TESTS
        ahfl.durable_store_import_provider_sdk_payload_plan.model.validate_ok
        ahfl.durable_store_import_provider_sdk_payload_plan.model.fail_forbidden_fields
        ahfl.durable_store_import_provider_sdk_payload_audit.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.32 v0.32-durable-store-import-provider-sdk-payload-golden
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_payload_plan.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_payload_audit.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_payload_plan.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_payload_audit.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_payload_plan.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_payload_audit.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.33 v0.33-durable-store-import-provider-sdk-mock-adapter-model
    TESTS
        ahfl.durable_store_import_provider_sdk_mock_adapter_contract.model.validate_ok
        ahfl.durable_store_import_provider_sdk_mock_adapter_execution.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_sdk_mock_adapter_readiness.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.33 v0.33-durable-store-import-provider-sdk-mock-adapter-golden
    TESTS
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_contract.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_execution.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_contract.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_execution.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_contract.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_execution.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_sdk_mock_adapter_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.34 v0.34-durable-store-import-provider-local-filesystem-alpha-model
    TESTS
        ahfl.durable_store_import_provider_local_filesystem_alpha_plan.model.validate_ok
        ahfl.durable_store_import_provider_local_filesystem_alpha_result.model.validate_dry_run_ok
        ahfl.durable_store_import_provider_local_filesystem_alpha_result.integration.opt_in_write_ok
        ahfl.durable_store_import_provider_local_filesystem_alpha_readiness.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.34 v0.34-durable-store-import-provider-local-filesystem-alpha-golden
    TESTS
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_plan.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_result.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_readiness.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_plan.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_result.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_readiness.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_plan.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_result.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_local_filesystem_alpha_readiness.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.35 v0.35-durable-store-import-provider-write-retry-model
    TESTS
        ahfl.durable_store_import_provider_write_retry_decision.model.validate_ok
        ahfl.durable_store_import_provider_write_retry_decision.bootstrap.matrix_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.35 v0.35-durable-store-import-provider-write-retry-golden
    TESTS
        ahflc.emit_durable_store_import_provider_write_retry_decision.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_retry_decision.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_retry_decision.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.36 v0.36-durable-store-import-provider-write-commit-model
    TESTS
        ahfl.durable_store_import_provider_write_commit_receipt.model.validate_ok
        ahfl.durable_store_import_provider_write_commit_receipt.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_write_commit_review.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.36 v0.36-durable-store-import-provider-write-commit-golden
    TESTS
        ahflc.emit_durable_store_import_provider_write_commit_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_commit_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_commit_receipt.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_commit_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_commit_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_write_commit_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.37 v0.37-durable-store-import-provider-write-recovery-model
    TESTS
        ahfl.durable_store_import_provider_write_recovery_checkpoint.model.validate_ok
        ahfl.durable_store_import_provider_write_recovery.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_write_recovery_review.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.37 v0.37-durable-store-import-provider-write-recovery-golden
    TESTS
        ahflc.emit_durable_store_import_provider_write_recovery_checkpoint.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_plan.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_checkpoint.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_plan.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_checkpoint.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_plan.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_write_recovery_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.38 v0.38-durable-store-import-provider-failure-taxonomy-model
    TESTS
        ahfl.durable_store_import_provider_failure_taxonomy_report.model.validate_ok
        ahfl.durable_store_import_provider_failure_taxonomy.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_failure_taxonomy_review.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.38 v0.38-durable-store-import-provider-failure-taxonomy-golden
    TESTS
        ahflc.emit_durable_store_import_provider_failure_taxonomy_report.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_failure_taxonomy_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_failure_taxonomy_report.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_failure_taxonomy_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_failure_taxonomy_report.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_failure_taxonomy_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.39 v0.39-durable-store-import-provider-audit-model
    TESTS
        ahfl.durable_store_import_provider_execution_audit_event.model.validate_ok
        ahfl.durable_store_import_provider_execution_audit_event.bootstrap.matrix_ok
        ahfl.durable_store_import_provider_telemetry_summary.model.validate_ok
        ahfl.durable_store_import_provider_operator_review_event.model.validate_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.39 v0.39-durable-store-import-provider-audit-golden
    TESTS
        ahflc.emit_durable_store_import_provider_execution_audit_event.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_telemetry_summary.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_operator_review_event.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_execution_audit_event.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_telemetry_summary.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_operator_review_event.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_execution_audit_event.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_telemetry_summary.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_operator_review_event.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.40 v0.40-durable-store-import-provider-compatibility-model
    TESTS
        ahfl.durable_store_import_provider_compatibility_test_manifest.model.validate_ok
        ahfl.durable_store_import_provider_fixture_matrix.model.validate_ok
        ahfl.durable_store_import_provider_compatibility_report.model.validate_ok
        ahfl.durable_store_import_provider_compatibility_report.bootstrap.blocked_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.40 v0.40-durable-store-import-provider-compatibility-golden
    TESTS
        ahflc.emit_durable_store_import_provider_compatibility_test_manifest.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_fixture_matrix.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_compatibility_report.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_compatibility_test_manifest.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_fixture_matrix.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_compatibility_report.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_compatibility_test_manifest.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_fixture_matrix.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_compatibility_report.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.41 v0.41-durable-store-import-provider-registry-model
    TESTS
        ahfl.durable_store_import_provider_registry.model.validate_ok
        ahfl.durable_store_import_provider_selection_plan.model.validate_ok
        ahfl.durable_store_import_provider_capability_negotiation_review.model.validate_ok
        ahfl.durable_store_import_provider_selection_plan.bootstrap.fallback_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.41 v0.41-durable-store-import-provider-registry-golden
    TESTS
        ahflc.emit_durable_store_import_provider_registry.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_selection_plan.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_capability_negotiation_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_registry.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_selection_plan.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_capability_negotiation_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_registry.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_selection_plan.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_capability_negotiation_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.42 v0.42-durable-store-import-provider-production-readiness-model
    TESTS
        ahfl.durable_store_import_provider_production_readiness_evidence.model.validate_ok
        ahfl.durable_store_import_provider_production_readiness_review.model.validate_ok
        ahfl.durable_store_import_provider_production_readiness_report.model.validate_ok
        ahfl.durable_store_import_provider_production_readiness.bootstrap.blocked_ok
)

ahfl_label_tests(
    LABELS ahfl-v0.42 v0.42-durable-store-import-provider-production-readiness-golden
    TESTS
        ahflc.emit_durable_store_import_provider_production_readiness_evidence.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_report.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_evidence.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_review.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_report.manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_evidence.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_review.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_provider_production_readiness_report.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.43 v0.43-durable-store-import-provider-conformance-golden
    TESTS
        ahflc.emit_durable_store_import_provider_conformance_report.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.44 v0.44-durable-store-import-provider-schema-compatibility-model
    TESTS
        ahfl.schema_compatibility.model.test_all_compatible
        ahfl.schema_compatibility.model.test_incompatible_version
        ahfl.schema_compatibility.model.test_source_chain_incompatible
        ahfl.schema_compatibility.model.test_empty_identity_fields
        ahfl.schema_compatibility.model.test_validation_wrong_format_version
)

ahfl_label_tests(
    LABELS ahfl-v0.45 v0.45-durable-store-import-provider-config-bundle-validation-model
    TESTS
        ahfl.config_bundle_validation.model.test_build_validation_report
        ahfl.config_bundle_validation.model.test_validate_format_version
        ahfl.config_bundle_validation.model.test_validate_security_constraints
        ahfl.config_bundle_validation.model.test_summary_counts
)

ahfl_label_tests(
    LABELS ahfl-v0.46 v0.46-release-evidence-archive-model
    TESTS
        ahfl.release_evidence_archive.model.test_build_manifest
        ahfl.release_evidence_archive.model.test_validate_format_version
        ahfl.release_evidence_archive.model.test_validate_empty_fields
        ahfl.release_evidence_archive.model.test_build_with_invalid_conformance
        ahfl.release_evidence_archive.model.test_invalid_conformance_evidence
        ahfl.release_evidence_archive.model.test_count_consistency
)

ahfl_label_tests(
    LABELS ahfl-v0.47 v0.47-approval-workflow-model
    TESTS
        ahfl.approval_workflow.model.test_build_approval_request
        ahfl.approval_workflow.model.test_build_receipt_rejected
        ahfl.approval_workflow.model.test_build_receipt_approved
        ahfl.approval_workflow.model.test_build_receipt_deferred
        ahfl.approval_workflow.model.test_validate_format_version
        ahfl.approval_workflow.model.test_validate_rejection_details
        ahfl.approval_workflow.model.test_validate_receipt_consistency
)

ahfl_label_tests(
    LABELS ahfl-v0.48 v0.48-opt-in-guard-model
    TESTS
        ahfl.opt_in_guard.model.test_build_all_gates_pass
        ahfl.opt_in_guard.model.test_build_deny_no_approval
        ahfl.opt_in_guard.model.test_build_deny_config_invalid
        ahfl.opt_in_guard.model.test_build_deny_registry_mismatch
        ahfl.opt_in_guard.model.test_build_deny_security_constraints
        ahfl.opt_in_guard.model.test_validate_format_version
        ahfl.opt_in_guard.model.test_validate_decision_consistency
        ahfl.opt_in_guard.model.test_default_deny
)

ahfl_label_tests(
    LABELS ahfl-v0.49 v0.49-runtime-policy-model
    TESTS
        ahfl.runtime_policy.model.test_build_all_gates_pass
        ahfl.runtime_policy.model.test_build_deny_opt_in_not_granted
        ahfl.runtime_policy.model.test_build_deny_approval_missing
        ahfl.runtime_policy.model.test_build_deny_config_invalid
        ahfl.runtime_policy.model.test_build_deny_registry_mismatch
        ahfl.runtime_policy.model.test_build_deny_readiness_not_met
        ahfl.runtime_policy.model.test_validate_format_version
        ahfl.runtime_policy.model.test_validate_decision_consistency
        ahfl.runtime_policy.model.test_default_deny
)

ahfl_label_tests(
    LABELS ahfl-v0.50 v0.50-production-integration-model
    TESTS
        ahfl.production_integration.model.test_build_all_evidence_pass
        ahfl.production_integration.model.test_build_blocked_conformance_fails
        ahfl.production_integration.model.test_build_blocked_schema_incompatible
        ahfl.production_integration.model.test_build_blocked_approval_rejected
        ahfl.production_integration.model.test_build_blocked_runtime_policy_deny
        ahfl.production_integration.model.test_validate_format_version
        ahfl.production_integration.model.test_validate_readiness_consistency
        ahfl.production_integration.model.test_default_safe_values
        ahfl.production_integration.model.test_validate_non_mutating_mode
)

ahfl_label_tests(
    LABELS ahfl-v0.51 v0.51-expression-evaluator
    TESTS
        ahfl.evaluator.eval_all
)

ahfl_label_tests(
    LABELS ahfl-rfc-corelib rfc-corelib-p7-runtime
    TESTS
        ahfl.evaluator.p7_runtime_all
)

ahfl_label_tests(
    LABELS ahfl-rfc-corelib rfc-corelib-p2-fn-generics-closures
    TESTS
        ahfl.semantics.fn_generics_closures_all
        ahfl.evaluator.generics_all
)

ahfl_label_tests(
    LABELS ahfl-rfc-corelib rfc-corelib-p3-trait-impl
    TESTS
        ahfl.semantics.trait_impl_all
)

ahfl_label_tests(
    LABELS ahfl-v0.52 v0.52-statement-executor
    TESTS
        ahfl.executor.exec_all
)

ahfl_label_tests(
    LABELS ahfl-v0.53 v0.53-agent-state-machine-runtime
    TESTS
        ahfl.runtime.agent_runtime_all
)

ahfl_label_tests(
    LABELS ahfl-v0.54 v0.54-workflow-integration
    TESTS
        ahfl.runtime.workflow_runtime_all
)

ahfl_label_tests(
    LABELS ahfl-v0.55 v0.55-capability-bridge
    TESTS
        ahfl.runtime.capability_bridge_all
)

ahfl_label_tests(
    LABELS ahfl-v0.55 v0.55-e2e
    TESTS
        ahfl.runtime.e2e_workflow
)

ahfl_label_tests(
    LABELS ahfl-v0.56 v0.56-llm-provider
    TESTS
        ahfl.llm_provider.all
        ahflc.run.llm_config.fail_missing_api_key_secret
        ahflc.run.llm_config.fail_invalid_budget
        ahflc.run.llm_config.fail_missing_fallback_api_key_secret
        ahflc.run.llm_config.fail_missing_vault_token_env
        ahflc.run.llm_tools.fail_invalid_capability_mocks
        ahflc.run.llm_observability.smoke
        ahflc.run.llm_failure_matrix.smoke
        ahflc.run.llm_secret_manager.smoke
        ahflc.run.capability_bindings.smoke
        ahflc.run.input_schema.fail_missing_field
)

ahfl_label_tests(
    LABELS ahfl-v0.57 v0.57-http-transport
    TESTS
        ahfl.runtime.http_transport_all
)

ahfl_label_tests(
    LABELS ahfl-v0.57 v0.57-grpc-transport
    TESTS
        ahfl.runtime.grpc_transport_all
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-json-dom
    TESTS
        ahfl.json.value_all
        ahfl.runtime.value_json_all
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-secret
    TESTS
        ahfl.secret.provider_all
        ahfl.secret.vault_rotation_all
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-passes
    TESTS
        ahfl.passes.pass_manager_all
        ahflc.passes.semantic_backend_effect
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-llm-streaming
    TESTS
        ahfl.llm_provider.streaming_all
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-lsp
    TESTS
        ahfl.lsp.json_rpc_all
        ahfl.lsp.process_smoke
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-lsp-handlers
    TESTS
        ahfl.lsp.handler_all
)

ahfl_label_tests(
    LABELS ahfl-v0.58 v0.58-connection-pool
    TESTS
        ahfl.runtime.connection_pool_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-error-recovery
    TESTS
        ahfl.frontend.error_recovery_all
)

# Wave-21 A-1: parser stack-depth guard (PARSER_STACK_OVERFLOW diagnostic)
ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-parser-hardening
    TESTS
        ahfl.frontend.parser_stack_depth_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-support-lib
    TESTS
        ahfl.support.thread_pool_all
        ahfl.support.version_all
        ahfl.support.diagnostic_serialization_all
        ahfl.support.trait_impl_diagnostics_all
        ahfl.support.diagnostics_code_smoke_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formal-bmc
    TESTS
        ahfl.formal.bmc_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formal-model-checker
    TESTS
        ahfl.formal.model_checker_backends_all
        ahflc.verify_formal.state_space_report
        ahflc.verify_formal.missing_binary
        ahflc.verify_formal.unsupported_backend
        ahflc.verify_formal.checker_error
        ahflc.verify_formal.checker_timeout
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formal-integration
    TESTS
        ahfl.formal.integration_improvement_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formal-counterexample
    TESTS
        ahfl.formal.counterexample_parse_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formal-bmc-depth
    TESTS
        ahfl.formal.bmc_depth_customization_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-parallel-scheduler
    TESTS
        ahfl.runtime.parallel_scheduler_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-sandbox
    TESTS
        ahfl.runtime.sandbox_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-distributed
    TESTS
        ahfl.runtime.distributed_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-formatter
    TESTS
        ahflc.fmt.formats_file_with_config
        ahflc.fmt.check_pass
        ahflc.fmt.check_fail
        ahflc.fmt.formats_directory
        ahflc.fmt.check_directory_fail
        ahflc.fmt.formats_manifest
        ahflc.fmt.formats_package_graph_workspace
        ahfl.formatter.formatter_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-repl
    TESTS
        ahfl.repl.repl_all
        ahfl.repl.process_smoke
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-dap
    TESTS
        ahfl.dap.basic_all
        ahfl.dap.process_smoke
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-telemetry
    TESTS
        ahfl.telemetry.telemetry_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-profiling
    TESTS
        ahfl.profiling.profiling_all
        ahflc.profile.time_passes.emit_summary
        ahflc.profile.time_passes.requires_optimize
        ahflc.profile.smv_size_report.emit_smv
        ahflc.profile.smv_size_report.rejects_non_smv
        ahflc.profile.observability_exports
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-abi
    TESTS
        ahfl.abi.compat_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-incremental
    TESTS
        ahfl.incremental.incremental_all
        ahfl.incremental.process_smoke
)

if(AHFL_ENABLE_BACKEND_INFRA)
    ahfl_label_tests(
        LABELS ahfl-v0.59 v0.59-wasm-backend
        TESTS
            ahfl.backends.wasm_all
    )

    ahfl_label_tests(
        LABELS ahfl-v0.59 v0.59-target-backends
        TESTS
            ahfl.backends.targets_all
    )
endif()

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-fuzzing
    TESTS
        ahfl.fuzz.parser_check
        ahfl.fuzz.typecheck_check
        ahfl.fuzz.smv_emitter_check
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-benchmarks
    TESTS
        ahfl.bench.compile_time
        ahfl.bench.memory_usage
        ahfl.bench.smv_size
        ahfl.bench.ir_pipeline
        ahflc.quality.smv_size_budget.flow_workflow
        ahflc.quality.smv_size_budget.pass_productization
        ahflc.quality.smv_size_budget.refund_audit
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-docs
    TESTS
        ahfl.docs.ir_sync_gate
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-package
    TESTS
        ahfl.package.package_all
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-property-testing
    TESTS
        ahfl.property.lowering_equiv
        ahfl.property.smv_syntax
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-mutation
    TESTS
        ahfl.mutation.config_report
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-quality-gates
    TESTS
        ahfl.fuzz.parser_check
        ahfl.fuzz.typecheck_check
        ahfl.fuzz.smv_emitter_check
        ahfl.property.lowering_equiv
        ahfl.property.smv_syntax
        ahfl.bench.compile_time
        ahfl.bench.memory_usage
        ahfl.bench.smv_size
        ahfl.bench.ir_pipeline
        ahflc.quality.smv_size_budget.flow_workflow
        ahflc.quality.smv_size_budget.pass_productization
        ahflc.quality.smv_size_budget.refund_audit
        ahfl.mutation.config_report
)

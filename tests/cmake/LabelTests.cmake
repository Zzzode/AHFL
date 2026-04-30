ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-model
    TESTS
        ahfl.frontend.project.fail_manifest_escape
        ahfl.frontend.project.fail_workspace_duplicate_project_name
        ahflc.dump_project.project_manifest.ok_cross_file
        ahflc.check.project_manifest.ok_cross_file
        ahflc.check.project_manifest.fail_escape
        ahflc.check.workspace.ok_cross_file
        ahflc.check.workspace.fail_duplicate_project_name
        ahflc.check.project_manifest.fail_invalid
        ahflc.check.workspace.fail_missing_project
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-debug
    TESTS
        ahflc.dump_project.project_manifest.ok_cross_file
        ahflc.dump_ast.project_manifest.ok_cross_file
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-semantics
    TESTS
        ahflc.check.project.ok_cross_file
        ahflc.check.project.fail_node_input
        ahflc.check.project_manifest.ok_cross_file
        ahflc.check.workspace.ok_cross_file
        ahfl.check.project.ok_expression_type_isolated
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-ir
    TESTS
        ahflc.emit_ir.workflow_value_flow
        ahflc.emit_ir_json.workflow_value_flow
        ahflc.emit_ir_json.project_manifest.workflow_value_flow
        ahfl.check.project.ok_expression_type_isolated
        ahfl.handoff.package_compat.escape_control_characters
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-backend
    TESTS
        ahflc.emit_summary.workflow_value_flow
        ahflc.emit_summary.project_manifest.workflow_value_flow
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
        ahflc.emit_native_json.project_manifest.workflow_value_flow
        ahflc.emit_native_json.workspace.workflow_value_flow
)

ahfl_label_tests(
    LABELS ahfl-v0.4 v0.4-package-compat
    TESTS
        ahfl.handoff.package_compat.normalize_identity_format_version
        ahfl.handoff.package_compat.omit_empty_provenance
        ahfl.handoff.package_compat.escape_control_characters
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-authoring-model
    TESTS
        ahfl.frontend.package_authoring.ok_basic
        ahfl.frontend.package_authoring.fail_unsupported_format
        ahfl.frontend.package_authoring.fail_duplicate_binding_key
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-authoring-emission
    TESTS
        ahflc.emit_native_json.workflow_value_flow.with_package
        ahflc.emit_native_json.project_manifest.workflow_value_flow.with_package
        ahflc.emit_native_json.workspace.workflow_value_flow.with_package
        ahflc.check.project_manifest.fail_package_without_native_json
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-authoring-validation
    TESTS
        ahfl.handoff.package.validate_normalizes_display_names
        ahfl.handoff.package.validate_rejects_wrong_kind
        ahfl.handoff.package.validate_rejects_duplicate_normalized_targets
        ahfl.handoff.package.validate_rejects_unknown_capability
        ahflc.emit_native_json.project_manifest.workflow_value_flow.with_display_package
        ahflc.emit_native_json.project_manifest.workflow_value_flow.fail_unknown_package_capability
)

ahfl_label_tests(
    LABELS ahfl-v0.5 v0.5-package-review
    TESTS
        ahflc.emit_package_review.workflow_value_flow.with_package
        ahflc.emit_package_review.project_manifest.workflow_value_flow.with_package
        ahflc.emit_package_review.project_manifest.workflow_value_flow.with_display_package
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
        ahflc.emit_package_review.project_manifest.workflow_value_flow.with_package
        ahflc.emit_package_review.project_manifest.workflow_value_flow.with_display_package
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
        ahflc.emit_execution_plan.project_manifest.workflow_value_flow.with_package
        ahflc.emit_execution_plan.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-execution-plan-validation
    TESTS
        ahfl.handoff.package.execution_plan.validate_project_workflow_value_flow
        ahfl.handoff.package.execution_plan.validate_fail_missing_entry_workflow
        ahfl.handoff.package.execution_plan.validate_fail_unknown_value_read
        ahflc.emit_execution_plan.project_manifest.workflow_value_flow.fail_agent_entry
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
        ahfl.dry_run.local.fail_missing_mock
        ahfl.dry_run.local.fail_unused_mock
)

ahfl_label_tests(
    LABELS ahfl-v0.6 v0.6-dry-run-trace
    TESTS
        ahflc.emit_dry_run_trace.workflow_value_flow.with_package
        ahflc.emit_dry_run_trace.project_manifest.workflow_value_flow.with_package
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
        ahflc.emit_runtime_session.project_manifest.workflow_value_flow.with_package
        ahflc.emit_runtime_session.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-runtime-session-emission
    TESTS
        ahflc.emit_runtime_session.alias_const.partial.with_package
        ahflc.emit_runtime_session.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_execution_journal.project_manifest.workflow_value_flow.with_package
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
        ahflc.emit_replay_view.project_manifest.workflow_value_flow.with_package
        ahflc.emit_replay_view.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-replay-view-emission
    TESTS
        ahflc.emit_replay_view.alias_const.partial.with_package
        ahflc.emit_replay_view.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_audit_report.project_manifest.workflow_value_flow.with_package
        ahflc.emit_audit_report.workspace.workflow_value_flow.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.9 v0.9-audit-report-emission
    TESTS
        ahflc.emit_audit_report.alias_const.partial.with_package
        ahflc.emit_audit_report.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_scheduler_snapshot.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-review-emission
    TESTS
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.10 v0.10-scheduler-golden
    TESTS
        ahflc.emit_scheduler_snapshot.workflow_value_flow.with_package
        ahflc.emit_scheduler_snapshot.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_scheduler_snapshot.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_scheduler_review.workflow_value_flow.with_package
        ahflc.emit_scheduler_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_checkpoint_record.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_checkpoint_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.11 v0.11-checkpoint-golden
    TESTS
        ahflc.emit_checkpoint_record.workflow_value_flow.with_package
        ahflc.emit_checkpoint_record.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_checkpoint_record.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_checkpoint_review.workflow_value_flow.with_package
        ahflc.emit_checkpoint_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_persistence_descriptor.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_export_manifest.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_export_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.13 v0.13-export-golden
    TESTS
        ahflc.emit_export_manifest.workflow_value_flow.with_package
        ahflc.emit_export_manifest.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_export_manifest.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_export_review.workflow_value_flow.with_package
        ahflc.emit_export_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_store_import_descriptor.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_store_import_review.workflow_value_flow.with_package
        ahflc.emit_store_import_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.14 v0.14-store-import-golden
    TESTS
        ahflc.emit_store_import_descriptor.workflow_value_flow.with_package
        ahflc.emit_store_import_descriptor.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_store_import_review.workflow_value_flow.with_package
        ahflc.emit_store_import_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_durable_store_import_request.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.15 v0.15-durable-store-import-golden
    TESTS
        ahflc.emit_durable_store_import_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_request.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_durable_store_import_receipt_persistence_request.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.18 v0.18-durable-store-import-receipt-persistence-golden
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_request.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_durable_store_import_decision.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_decision_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.16 v0.16-durable-store-import-decision-golden
    TESTS
        ahflc.emit_durable_store_import_decision.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_decision_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_decision_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-emission
    TESTS
        ahflc.emit_durable_store_import_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.17 v0.17-durable-store-import-receipt-golden
    TESTS
        ahflc.emit_durable_store_import_receipt.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-review-emission
    TESTS
        ahflc.emit_persistence_review.workflow_value_flow.with_package
        ahflc.emit_persistence_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.12 v0.12-persistence-golden
    TESTS
        ahflc.emit_persistence_descriptor.workflow_value_flow.with_package
        ahflc.emit_persistence_descriptor.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_persistence_descriptor.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_persistence_review.workflow_value_flow.with_package
        ahflc.emit_persistence_review.project_manifest.workflow_value_flow.failed.with_package
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
        ahflc.emit_durable_store_import_receipt_persistence_response.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
)

ahfl_label_tests(
    LABELS ahfl-v0.19 v0.19-durable-store-import-receipt-persistence-response-golden
    TESTS
        ahflc.emit_durable_store_import_receipt_persistence_response.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workflow_value_flow.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.project_manifest.workflow_value_flow.failed.with_package
        ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
)

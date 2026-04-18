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

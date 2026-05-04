add_test(NAME ahfl.frontend.project.ok_basic
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            ok-basic
            "${AHFL_TESTS_DIR}/project/ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/ok"
)

add_test(NAME ahfl.frontend.project.fail_missing
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-missing
            "${AHFL_TESTS_DIR}/project/missing/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/missing"
)

add_test(NAME ahfl.frontend.project.fail_mismatch
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-mismatch
            "${AHFL_TESTS_DIR}/project/mismatch/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/mismatch"
)

add_test(NAME ahfl.frontend.project.fail_no_module
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-no-module
            "${AHFL_TESTS_DIR}/project/no_module/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/no_module"
)

add_test(NAME ahfl.frontend.project.fail_duplicate_owner
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-duplicate-owner
            "${AHFL_TESTS_DIR}/project/duplicate_owner/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/duplicate_owner"
)

add_test(NAME ahfl.frontend.project.fail_manifest_escape
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-manifest-escape
            "${AHFL_TESTS_DIR}/project/manifest_escape/ahfl.project.json"
)

add_test(NAME ahfl.frontend.project.fail_workspace_duplicate_project_name
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-workspace-duplicate-project-name
            "${AHFL_TESTS_DIR}/project/workspace_duplicate/ahfl.workspace.json"
)

add_test(NAME ahfl.frontend.package_authoring.ok_basic
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            ok-package-authoring
            "${AHFL_TESTS_DIR}/project/package_authoring_ok/ahfl.package.json"
)

add_test(NAME ahfl.frontend.package_authoring.fail_unsupported_format
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-package-unsupported-format
            "${AHFL_TESTS_DIR}/project/package_authoring_bad_format/ahfl.package.json"
)

add_test(NAME ahfl.frontend.package_authoring.fail_duplicate_binding_key
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-package-duplicate-binding-key
            "${AHFL_TESTS_DIR}/project/package_authoring_duplicate_binding/ahfl.package.json"
)

add_test(NAME ahfl.support.diagnostics.metadata_smoke
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            diagnostics-support-metadata-smoke
            "${AHFL_TESTS_DIR}"
)

add_test(NAME ahfl.support.source.position_smoke
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            source-file-position-smoke
            "${AHFL_TESTS_DIR}"
)

add_test(NAME ahfl.resolver.project.ok_basic
    COMMAND $<TARGET_FILE:ahfl_project_resolve_tests>
            ok-basic
            "${AHFL_TESTS_DIR}/project/ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/ok"
)

add_test(NAME ahfl.resolver.project.ok_duplicate_locals
    COMMAND $<TARGET_FILE:ahfl_project_resolve_tests>
            ok-duplicate-locals
            "${AHFL_TESTS_DIR}/project/duplicate_locals/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/duplicate_locals"
)

add_test(NAME ahfl.resolver.project.fail_unknown_type
    COMMAND $<TARGET_FILE:ahfl_project_resolve_tests>
            fail-unknown-type
            "${AHFL_TESTS_DIR}/project/resolve_error/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/resolve_error"
)

add_test(NAME ahfl.check.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-cross-file
            "${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/check_ok"
)

add_test(NAME ahfl.check.project.fail_node_input
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            fail-node-input
            "${AHFL_TESTS_DIR}/project/check_fail_input/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/check_fail_input"
)

add_test(NAME ahfl.check.project.fail_completed_state
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            fail-completed-state
            "${AHFL_TESTS_DIR}/project/check_fail_state/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/check_fail_state"
)

add_test(NAME ahfl.check.project.ok_expression_type_isolated
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-expression-type-isolated
            "${AHFL_TESTS_DIR}/project/expression_type_isolated/app/main.ahfl"
            "${AHFL_TESTS_DIR}/project/expression_type_isolated"
)

add_test(NAME ahfl.handoff.package.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.validate_normalizes_display_names
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-package-normalizes-display-names
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.package_reader_summary.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            package-reader-summary-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.package_reader_summary.fail_missing_export
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            package-reader-summary-rejects-missing-export
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            execution-planner-bootstrap-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.fail_agent_entry
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            execution-planner-bootstrap-rejects-agent-entry
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.fail_missing_dependency
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            execution-planner-bootstrap-rejects-missing-dependency
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_plan.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            execution-plan-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_plan.fail_agent_entry
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            execution-plan-rejects-agent-entry
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-execution-plan-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_fail_missing_entry_workflow
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-execution-plan-rejects-missing-entry-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_fail_unknown_value_read
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-execution-plan-rejects-unknown-value-read
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.validate_rejects_wrong_kind
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-package-rejects-wrong-kind
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.validate_rejects_duplicate_normalized_targets
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-package-rejects-duplicate-normalized-targets
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.validate_rejects_unknown_capability
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            validate-package-rejects-unknown-capability
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.handoff.package.file_expr_temporal
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            file-expr-temporal
            "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
)

add_test(NAME ahfl.dry_run.local.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.dry_run.local.fail_missing_workflow
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-missing-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.dry_run.mock_set.parse_ok
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            parse-capability-mock-set-ok
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.dry_run.mock_set.parse_fail_duplicate_selector
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            parse-capability-mock-set-rejects-duplicate-selector
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.dry_run.local.fail_missing_mock
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-missing-mock
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.dry_run.local.fail_unused_mock
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-unused-mock
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            build-runtime-session-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.model.fail_missing_workflow
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            build-runtime-session-rejects-missing-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.model.fail_missing_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            build-runtime-session-rejects-missing-mock
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.model.partial_pending_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            build-runtime-session-partial-on-pending-mock
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.model.fail_node_failure
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            build-runtime-session-failed-on-node-failure
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.fail_incomplete_completed_workflow
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-rejects-incomplete-completed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.fail_missing_used_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-rejects-missing-used-mock
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.partial_workflow_ok
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-accepts-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.failed_workflow_ok
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-accepts-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.runtime_session.validation.fail_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_runtime_session_tests>
            validate-runtime-session-rejects-failed-without-failure-summary
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.execution_journal.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-ok
)

add_test(NAME ahfl.execution_journal.model.failure_path_ok
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-failure-path-ok
)

add_test(NAME ahfl.execution_journal.model.fail_out_of_order_node_phase
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-rejects-out-of-order-node-phase
)

add_test(NAME ahfl.execution_journal.model.fail_failed_before_node_started
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-rejects-failed-before-node-started
)

add_test(NAME ahfl.execution_journal.model.fail_failed_without_workflow_failed
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-rejects-failed-without-workflow-failed
)

add_test(NAME ahfl.execution_journal.model.fail_non_monotonic_execution_index
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-rejects-non-monotonic-execution-index
)

add_test(NAME ahfl.execution_journal.model.fail_events_after_workflow_completed
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            validate-execution-journal-rejects-events-after-workflow-completed
)

add_test(NAME ahfl.execution_journal.bootstrap.from_runtime_session_ok
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            build-execution-journal-from-runtime-session-ok
)

add_test(NAME ahfl.execution_journal.bootstrap.from_failed_runtime_session_ok
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            build-execution-journal-from-failed-runtime-session-ok
)

add_test(NAME ahfl.execution_journal.bootstrap.from_partial_runtime_session_ok
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            build-execution-journal-from-partial-runtime-session-ok
)

add_test(NAME ahfl.execution_journal.bootstrap.fail_invalid_runtime_session
    COMMAND $<TARGET_FILE:ahfl_execution_journal_tests>
            build-execution-journal-rejects-invalid-runtime-session
)

add_test(NAME ahfl.replay_view.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            build-replay-view-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            build-replay-view-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            build-replay-view-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.model.fail_invalid_execution_journal
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            build-replay-view-rejects-invalid-execution-journal
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.model.fail_session_journal_mismatch
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            build-replay-view-rejects-session-journal-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.validation.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            validate-replay-view-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.validation.fail_missing_completed_progression
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            validate-replay-view-rejects-missing-completed-progression
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.replay_view.validation.fail_execution_order_index_mismatch
    COMMAND $<TARGET_FILE:ahfl_replay_view_tests>
            validate-replay-view-rejects-execution-order-index-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.audit_report.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            validate-audit-report-ok
)

add_test(NAME ahfl.audit_report.model.fail_unknown_session_node
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            validate-audit-report-rejects-unknown-session-node
)

add_test(NAME ahfl.audit_report.model.fail_journal_order_mismatch
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            validate-audit-report-rejects-journal-order-mismatch
)

add_test(NAME ahfl.audit_report.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.audit_report.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.audit_report.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.audit_report.bootstrap.fail_trace_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-rejects-trace-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.audit_report.bootstrap.fail_trace_execution_order_mismatch
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-rejects-trace-execution-order-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_snapshot.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-ok
)

add_test(NAME ahfl.scheduler_snapshot.model.validate_failed_ok
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-failed-ok
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_runnable_without_ready_nodes
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-runnable-without-ready-nodes
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_next_candidate_not_ready
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-next-candidate-not-ready
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_non_prefix_cursor
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-non-prefix-cursor
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_ready_dependency_outside_planned_set
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-ready-dependency-outside-planned-set
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_unknown_ready_node
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-unknown-ready-node
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_blocked_terminal_failure_without_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-blocked-terminal-failure-without-summary
)

add_test(NAME ahfl.scheduler_snapshot.model.fail_terminal_completed_without_full_prefix
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-terminal-completed-without-full-prefix
)

add_test(NAME ahfl.scheduler_snapshot.compat.fail_unsupported_format_version
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-unsupported-format-version
)

add_test(NAME ahfl.scheduler_snapshot.compat.fail_unsupported_source_replay_view_format_version
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-snapshot-rejects-unsupported-source-replay-view-format-version
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.fail_replay_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-rejects-replay-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_review.model.completed_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-completed
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_review.model.failed_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-failed
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_review.model.partial_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-partial
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.scheduler_review.model.fail_invalid_snapshot
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-rejects-invalid-snapshot
)

add_test(NAME ahfl.scheduler_review.compat.validate_ok
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-decision-summary-ok
)

add_test(NAME ahfl.scheduler_review.compat.fail_unsupported_format_version
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-decision-summary-rejects-unsupported-format-version
)

add_test(NAME ahfl.scheduler_review.compat.fail_unsupported_source_snapshot_format_version
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-decision-summary-rejects-unsupported-source-snapshot-format-version
)

add_test(NAME ahfl.scheduler_review.compat.fail_prefix_size_mismatch
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-decision-summary-rejects-prefix-size-mismatch
)

add_test(NAME ahfl.scheduler_review.compat.fail_runnable_terminal_reason
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            validate-scheduler-decision-summary-rejects-runnable-terminal-reason
)

add_test(NAME ahfl.checkpoint_record.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-ok
)

add_test(NAME ahfl.checkpoint_record.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-blocked-ok
)

add_test(NAME ahfl.checkpoint_record.model.validate_terminal_completed_ok
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-terminal-completed-ok
)

add_test(NAME ahfl.checkpoint_record.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-terminal-failed-ok
)

add_test(NAME ahfl.checkpoint_record.model.fail_non_prefix_cursor
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-rejects-non-prefix-cursor
)

add_test(NAME ahfl.checkpoint_record.model.fail_resume_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-rejects-resume-ready-with-blocker
)

add_test(NAME ahfl.checkpoint_record.model.fail_terminal_completed_without_full_prefix
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-rejects-terminal-completed-without-full-prefix
)

add_test(NAME ahfl.checkpoint_record.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.checkpoint_record.model.fail_durable_adjacent_without_checkpoint_friendly_source
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-record-rejects-durable-adjacent-without-checkpoint-friendly-source
)

add_test(NAME ahfl.checkpoint_record.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.fail_snapshot_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-rejects-snapshot-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_review.model.completed_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-completed
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_review.model.failed_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-failed
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_review.model.partial_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-partial
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.checkpoint_review.model.fail_invalid_record
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-rejects-invalid-record
)

add_test(NAME ahfl.checkpoint_review.compat.validate_ok
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-review-ok
)

add_test(NAME ahfl.checkpoint_review.compat.fail_unsupported_format_version
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-review-rejects-unsupported-format-version
)

add_test(NAME ahfl.checkpoint_review.compat.fail_unsupported_source_record_format_version
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-review-rejects-unsupported-source-record-format-version
)

add_test(NAME ahfl.checkpoint_review.compat.fail_prefix_size_mismatch
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-review-rejects-prefix-size-mismatch
)

add_test(NAME ahfl.checkpoint_review.compat.fail_ready_terminal_reason
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            validate-checkpoint-review-rejects-ready-terminal-reason
)

add_test(NAME ahfl.persistence_descriptor.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-blocked-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-terminal-failed-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-terminal-partial-ok
)

add_test(NAME ahfl.persistence_descriptor.model.fail_missing_planned_identity
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-missing-planned-identity
)

add_test(NAME ahfl.persistence_descriptor.model.fail_non_prefix_cursor
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-non-prefix-cursor
)

add_test(NAME ahfl.persistence_descriptor.model.fail_export_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-export-ready-with-blocker
)

add_test(NAME ahfl.persistence_descriptor.model.fail_unsupported_checkpoint_record_format
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-unsupported-checkpoint-record-format
)

add_test(NAME ahfl.persistence_descriptor.model.fail_ready_from_blocked_checkpoint
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-ready-from-blocked-checkpoint
)

add_test(NAME ahfl.persistence_descriptor.model.fail_terminal_failed_export_ready
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-terminal-failed-export-ready
)

add_test(NAME ahfl.persistence_descriptor.model.fail_store_adjacent_blocked
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-store-adjacent-blocked
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-descriptor-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-descriptor-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-descriptor-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.fail_invalid_checkpoint_record
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-descriptor-rejects-invalid-checkpoint-record
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.fail_checkpoint_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-descriptor-rejects-checkpoint-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-review-ok
)

add_test(NAME ahfl.persistence_review.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            validate-persistence-review-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.persistence_review.model.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-review-rejects-invalid-descriptor
)

add_test(NAME ahfl.persistence_review.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-review-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-review-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_descriptor_tests>
            build-persistence-review-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-blocked-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-terminal-failed-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-terminal-partial-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_missing_export_package_identity
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-missing-export-package-identity
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_duplicate_artifact_name
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-duplicate-artifact-name
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-ready-with-blocker
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_ready_from_blocked_persistence
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-ready-from-blocked-persistence
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_store_import_adjacent_blocked
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-store-import-adjacent-blocked
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-manifest-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-manifest-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-manifest-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-manifest-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-manifest-rejects-invalid-descriptor
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.fail_descriptor_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-manifest-rejects-descriptor-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-review-ok
)

add_test(NAME ahfl.persistence_export_review.model.fail_unsupported_source_manifest_format
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            validate-persistence-export-review-rejects-unsupported-source-manifest-format
)

add_test(NAME ahfl.persistence_export_review.model.fail_invalid_manifest
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-review-rejects-invalid-manifest
)

add_test(NAME ahfl.persistence_export_review.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-review-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-review-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.persistence_export_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_persistence_export_tests>
            build-persistence-export-review-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_descriptor.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-ok
)

add_test(NAME ahfl.store_import_descriptor.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-blocked-ok
)

add_test(NAME ahfl.store_import_descriptor.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-terminal-failed-ok
)

add_test(NAME ahfl.store_import_descriptor.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-terminal-partial-ok
)

add_test(NAME ahfl.store_import_descriptor.model.fail_missing_candidate_identity
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-missing-candidate-identity
)

add_test(NAME ahfl.store_import_descriptor.model.fail_duplicate_artifact_name
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-duplicate-artifact-name
)

add_test(NAME ahfl.store_import_descriptor.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-ready-with-blocker
)

add_test(NAME ahfl.store_import_descriptor.model.fail_ready_from_blocked_manifest
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-ready-from-blocked-manifest
)

add_test(NAME ahfl.store_import_descriptor.model.fail_unsupported_source_manifest_format
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-unsupported-source-manifest-format
)

add_test(NAME ahfl.store_import_descriptor.model.fail_adapter_consumable_blocked
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-adapter-consumable-blocked
)

add_test(NAME ahfl.store_import_descriptor.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-descriptor-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.fail_invalid_manifest
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-rejects-invalid-manifest
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.fail_manifest_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-rejects-manifest-workflow-mismatch
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-review-ok
)

add_test(NAME ahfl.store_import_review.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            validate-store-import-review-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.store_import_review.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-project-workflow-value-flow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-failed-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-partial-workflow
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.store_import_review.model.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-rejects-invalid-descriptor
            "${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
)

add_test(NAME ahfl.durable_store_import_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-terminal-failed-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-terminal-partial-ok
)

add_test(NAME ahfl.durable_store_import_request.model.fail_missing_request_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-missing-request-identity
)

add_test(NAME ahfl.durable_store_import_request.model.fail_duplicate_artifact_name
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-duplicate-artifact-name
)

add_test(NAME ahfl.durable_store_import_request.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_request.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.durable_store_import_request.model.fail_adapter_contract_blocked
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-adapter-contract-blocked
)

add_test(NAME ahfl.durable_store_import_request.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-request-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.ready_descriptor
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-request-ready-descriptor
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.completed_descriptor
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-request-completed-descriptor
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-request-rejects-invalid-descriptor
)

add_test(NAME ahfl.durable_store_import_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-review-ok
)

add_test(NAME ahfl.durable_store_import_review.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            validate-durable-store-import-review-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_review.model.ready_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-review-ready-request
)

add_test(NAME ahfl.durable_store_import_review.model.failed_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-review-failed-request
)

add_test(NAME ahfl.durable_store_import_review.model.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_tests>
            build-durable-store-import-review-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-blocked-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-deferred-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejected-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_missing_decision_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-missing-decision-identity
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_accepted_with_blocker
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-accepted-with-blocker
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_adapter_decision_consumable_blocked
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-adapter-decision-consumable-blocked
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_rejected_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-rejected-without-failure-summary
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_missing_receipt_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-missing-receipt-identity
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_unsupported_source_decision_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-unsupported-source-decision-format
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.ready_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-ready-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.blocked_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-blocked-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.deferred_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-deferred-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.rejected_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-rejected-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.fail_invalid_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-rejects-invalid-decision
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_missing_request_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-missing-request-identity
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_unsupported_source_receipt_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-unsupported-source-receipt-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-ready-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.blocked_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-blocked-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.deferred_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-deferred-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.rejected_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-rejected-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-review-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-ready-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.rejected_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-rejected-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_missing_response_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-missing-response-identity
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_accepted_with_blocker
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-accepted-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-ready-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.ready_response
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-ready-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.rejected_response
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-rejected-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.fail_invalid_response
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-rejects-invalid-response
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-blocked-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-deferred-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejected-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_missing_persistence_id
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-missing-persistence-id
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_non_mutating_accepted
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-non-mutating-accepted
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_mutated_non_accepted
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-mutated-non-accepted
)

add_test(NAME ahfl.durable_store_import_adapter_execution.bootstrap.ready_response
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-adapter-execution-ready-response
)

add_test(NAME ahfl.durable_store_import_adapter_execution.bootstrap.fail_invalid_response
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-adapter-execution-rejects-invalid-response
)

add_test(NAME ahfl.durable_store_import_recovery_preview.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-recovery-preview-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.ready_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-ready-execution-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.rejected_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-rejected-execution-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.fail_invalid_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-rejects-invalid-execution
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejected-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_adapter_config.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-adapter-config-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_adapter_config.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-adapter-config-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.fail_planned_without_provider_id
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejects-planned-without-provider-id
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.fail_not_planned_mutating_intent
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejects-not-planned-mutating-intent
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.bootstrap.ready_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-ready-execution
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.bootstrap.fail_invalid_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-rejects-invalid-execution
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-recovery-handoff-ok
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-recovery-handoff-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.bootstrap.fail_invalid_write_attempt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-recovery-handoff-rejects-invalid-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejected-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.missing_profile_load_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-missing-profile-load-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.profile_mismatch
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-profile-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_profile.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-profile-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_driver_profile.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-profile-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_sdk_invocation
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-sdk-invocation
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_descriptor
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-bound-without-descriptor
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_profile_load
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-bound-without-profile-load
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.bootstrap.ready_write_attempt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-ready-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.bootstrap.fail_invalid_write_attempt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-rejects-invalid-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.bootstrap.fail_invalid_binding_plan
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-readiness-rejects-invalid-binding-plan
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.profile_mismatch
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-profile-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_profile.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-profile-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_runtime_profile.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-profile-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.fail_ready_without_envelope
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-rejects-ready-without-envelope
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.bootstrap.ready_binding
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-ready-binding
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.bootstrap.fail_invalid_driver_binding
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-rejects-invalid-driver-binding
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.bootstrap.fail_invalid_preflight
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-readiness-rejects-invalid-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.policy_mismatch
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-policy-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_policy.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-policy-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_policy.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-policy-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.fail_ready_without_handoff
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-rejects-ready-without-handoff
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.bootstrap.ready_preflight
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-ready-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.bootstrap.fail_invalid_preflight
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-rejects-invalid-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-handoff-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-handoff-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.fail_invalid_envelope
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-handoff-readiness-rejects-invalid-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.policy_mismatch
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-policy-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_policy.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-policy-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_host_policy.model.fail_host_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-policy-rejects-host-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.fail_ready_without_descriptor
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-rejects-ready-without-descriptor
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.bootstrap.ready_envelope
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-ready-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.bootstrap.fail_invalid_envelope
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-rejects-invalid-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.fail_invalid_host_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-readiness-rejects-invalid-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_ready_without_identity
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-rejects-ready-without-identity
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.ready_host_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-ready-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.fail_invalid_host_execution
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-rejects-invalid-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-review-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-review-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-request-ready-receipt
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-response-placeholder-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-response-placeholder-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_readiness.bootstrap.fail_invalid_placeholder
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-readiness-rejects-invalid-placeholder
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-interface-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface_review.bootstrap.fail_invalid_plan
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-interface-review-rejects-invalid-plan
)

add_test(NAME ahfl.durable_store_import_provider_config_load.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-load-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_load.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-load-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_config_load.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-config-load-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_snapshot.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-snapshot-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_readiness.bootstrap.fail_invalid_snapshot
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-config-readiness-rejects-invalid-snapshot
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-secret-resolver-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_response.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-response-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_policy_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-policy-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_policy_review.bootstrap.fail_invalid_placeholder
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-provider-secret-policy-review-rejects-invalid-placeholder
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_request.model.fail_sandbox_escape
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-request-rejects-sandbox-escape
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_record.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-record-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_record.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            run-durable-store-import-provider-local-host-harness-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_plan.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-plan-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_plan.model.fail_forbidden_fields
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-plan-rejects-forbidden-fields
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_audit.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-audit-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_contract.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-mock-adapter-contract-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_execution.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            run-durable-store-import-provider-sdk-mock-adapter-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-mock-adapter-readiness-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.model.fail_unsupported_source_receipt_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-review-rejects-unsupported-source-receipt-format
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-ready-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.rejected_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-rejected-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-ready-request
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.completed_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-completed-request
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_decision_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-review-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.model.fail_unsupported_source_decision_format
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            validate-durable-store-import-decision-review-rejects-unsupported-source-decision-format
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.accepted_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-accepted-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.rejected_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-rejected-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.fail_invalid_decision
    COMMAND $<TARGET_FILE:ahfl_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-rejects-invalid-decision
)

add_test(NAME ahfl.handoff.package_compat.normalize_identity_format_version
    COMMAND $<TARGET_FILE:ahfl_handoff_package_compat_tests>
            normalize-identity-format-version
)

add_test(NAME ahfl.handoff.package_compat.omit_empty_provenance
    COMMAND $<TARGET_FILE:ahfl_handoff_package_compat_tests>
            omit-empty-provenance
)

add_test(NAME ahfl.handoff.package_compat.escape_control_characters
    COMMAND $<TARGET_FILE:ahfl_handoff_package_compat_tests>
            escape-control-characters
)

add_test(NAME ahflc.dump_project.ok_basic
    COMMAND $<TARGET_FILE:ahflc> dump-project
            --search-root "${AHFL_TESTS_DIR}/project/ok"
            "${AHFL_TESTS_DIR}/project/ok/app/main.ahfl"
)
set_tests_properties(ahflc.dump_project.ok_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "source_graph \\(1 entry, 2 sources, 1 import\\)"
)

add_test(NAME ahflc.check.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> check
            --search-root "${AHFL_TESTS_DIR}/project/check_ok"
            "${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
)
set_tests_properties(ahflc.check.project.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\), 9 symbol\\(s\\), 13 reference\\(s\\), 3 named type\\(s\\)"
)

add_test(NAME ahflc.dump_types.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> dump-types
            --search-root "${AHFL_TESTS_DIR}/project/check_ok"
            "${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
)
set_tests_properties(ahflc.dump_types.project.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow app::main::MainWorkflow"
)

add_test(NAME ahflc.dump_project.project_manifest.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> dump-project
            --project "${AHFL_TESTS_DIR}/project/check_ok/ahfl.project.json"
)
set_tests_properties(ahflc.dump_project.project_manifest.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "source_graph \\(1 entry, 3 sources, 3 imports\\)"
)

add_test(NAME ahflc.check.project_manifest.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> check
            --project "${AHFL_TESTS_DIR}/project/check_ok/ahfl.project.json"
)
set_tests_properties(ahflc.check.project_manifest.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\), 9 symbol\\(s\\), 13 reference\\(s\\), 3 named type\\(s\\)"
)

add_test(NAME ahflc.check.workspace.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> check
            --workspace "${AHFL_TESTS_DIR}/project/ahfl.workspace.json"
            --project-name check-ok
)
set_tests_properties(ahflc.check.workspace.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\), 9 symbol\\(s\\), 13 reference\\(s\\), 3 named type\\(s\\)"
)

add_test(NAME ahflc.dump_ast.project_manifest.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=dump-ast --project ${AHFL_TESTS_DIR}/project/check_ok/ahfl.project.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/project/project_check_ok.ast"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_ir.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit-ir"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/project/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/ir/project_check_ok.ir"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_ir_json.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit-ir-json"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/project/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/ir/project_check_ok.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_ir_json.project_manifest.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-ir-json --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/ir/project_workflow_value_flow.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.project.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit-native-json"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workflow_value_flow/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/project/workflow_value_flow"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.project_manifest.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.with_package.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.project_manifest.workflow_value_flow.with_display_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.display.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.with_package.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_package_review.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-package-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/review/project_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_package_review.project_manifest.workflow_value_flow.with_display_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-package-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.display.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/review/project_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_plan.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-plan --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/plan/project_workflow_value_flow.with_package.execution-plan.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_dry_run_trace.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-dry-run-trace --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/trace/project_workflow_value_flow.with_package.dry-run-trace.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/project_workflow_value_flow.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/project_workflow_value_flow.failed.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_journal.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-journal --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/journal/project_workflow_value_flow.with_package.execution-journal.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/project_workflow_value_flow.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/project_workflow_value_flow.failed.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.project_manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/project_workflow_value_flow.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/project_workflow_value_flow.failed.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_snapshot.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-snapshot --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/project_workflow_value_flow.failed.with_package.scheduler-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_record.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-record --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/project_workflow_value_flow.failed.with_package.checkpoint-record.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/project_workflow_value_flow.failed.with_package.checkpoint-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_descriptor.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-persistence-descriptor --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/persistence/project_workflow_value_flow.failed.with_package.persistence-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-persistence-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/persistence/project_workflow_value_flow.failed.with_package.persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_manifest.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-export-manifest --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/export/project_workflow_value_flow.failed.with_package.export-manifest.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-export-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/export/project_workflow_value_flow.failed.with_package.export-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_descriptor.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-store-import-descriptor --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/store_import/project_workflow_value_flow.failed.with_package.store-import-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-store-import-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/store_import/project_workflow_value_flow.failed.with_package.store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_request.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-request --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_decision.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-decision --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_request.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-request --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-response --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-response.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-response-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-response-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_adapter_execution.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-adapter-execution --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-adapter-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_recovery_preview.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-recovery-preview --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-recovery-preview"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_write_attempt.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-write-attempt --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-write-attempt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_recovery_handoff.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-recovery-handoff --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-recovery-handoff"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_binding.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-driver-binding --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-driver-binding.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-driver-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-driver-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_preflight.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-runtime-preflight --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-runtime-preflight.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-runtime-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-runtime-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_envelope.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-envelope --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-envelope.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-handoff-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-handoff-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-host-execution --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-host-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-host-execution-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-host-execution-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-execution-receipt --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-local-host-execution-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-execution-receipt-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-local-host-execution-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_request.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-request --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-response-placeholder --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-response-placeholder.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-interface --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-interface.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-interface-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-interface-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

function(ahfl_add_provider_v30_v33_project_cli_tests VARIANT_NAME AHFL_ARGS EXPECTED_PREFIX)
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_resolver_request.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-secret-resolver-request ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-resolver-request.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_resolver_response.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-secret-resolver-response ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-resolver-response.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_policy_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-secret-policy-review ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-policy-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_request.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-harness-request ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-request.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_record.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-harness-record ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-record.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-harness-review ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_payload_plan.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-payload-plan ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-payload-plan.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_payload_audit.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-payload-audit ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-payload-audit"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_contract.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-mock-adapter-contract ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-contract.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_execution.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-mock-adapter-execution ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-execution.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_readiness.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-mock-adapter-readiness ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-readiness"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
endfunction()

add_test(NAME ahflc.emit_durable_store_import_provider_config_load.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-load --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-load.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_snapshot.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-snapshot --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_readiness.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-readiness --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

ahfl_add_provider_v30_v33_project_cli_tests(
    project_manifest.workflow_value_flow.failed.with_package
    "--project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
    "project_workflow_value_flow.failed.with_package"
)

add_test(NAME ahflc.emit_durable_store_import_decision_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-decision-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_review.project_manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-review --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/project_workflow_value_flow.failed.with_package.scheduler-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.workspace.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.with_package.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_package_review.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-package-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/review/project_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_plan.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-plan --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/plan/project_workflow_value_flow.with_package.execution-plan.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_dry_run_trace.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-dry-run-trace --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/trace/project_workflow_value_flow.with_package.dry-run-trace.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/project_workflow_value_flow.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/project_workflow_value_flow.partial.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_journal.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-journal --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/journal/project_workflow_value_flow.with_package.execution-journal.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/project_workflow_value_flow.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/project_workflow_value_flow.partial.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/project_workflow_value_flow.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/project_workflow_value_flow.partial.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-snapshot --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/project_workflow_value_flow.partial.with_package.scheduler-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_record.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-record --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/project_workflow_value_flow.partial.with_package.checkpoint-record.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/project_workflow_value_flow.partial.with_package.checkpoint-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_descriptor.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-persistence-descriptor --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/persistence/project_workflow_value_flow.partial.with_package.persistence-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-persistence-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/persistence/project_workflow_value_flow.partial.with_package.persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_manifest.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-export-manifest --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/export/project_workflow_value_flow.partial.with_package.export-manifest.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-export-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/export/project_workflow_value_flow.partial.with_package.export-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-store-import-descriptor --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/store_import/project_workflow_value_flow.partial.with_package.store-import-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-store-import-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/store_import/project_workflow_value_flow.partial.with_package.store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-request --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-decision --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-request --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-response --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-response.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-receipt-persistence-response-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-response-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_adapter_execution.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-adapter-execution --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-adapter-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_recovery_preview.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-recovery-preview --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-recovery-preview"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_write_attempt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-write-attempt --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-write-attempt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_recovery_handoff.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-recovery-handoff --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-recovery-handoff"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_binding.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-driver-binding --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-driver-binding.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-driver-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-driver-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_preflight.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-runtime-preflight --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-runtime-preflight.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-runtime-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-runtime-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_envelope.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-envelope --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-envelope.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-handoff-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-handoff-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-host-execution --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-host-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-host-execution-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-host-execution-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-execution-receipt --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-local-host-execution-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-local-host-execution-receipt-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-local-host-execution-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-request --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-response-placeholder --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-response-placeholder.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-interface --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-interface.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-sdk-adapter-interface-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-interface-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_load.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-load --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-load.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_snapshot.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-snapshot --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-provider-config-readiness --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

ahfl_add_provider_v30_v33_project_cli_tests(
    workspace.workflow_value_flow.partial.with_package
    "--workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
    "project_workflow_value_flow.partial.with_package"
)

add_test(NAME ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-durable-store-import-decision-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-review --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow --package ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/project_workflow_value_flow.partial.with_package.scheduler-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_summary.project_manifest.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-summary --project ${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/summary/project_workflow_value_flow.summary"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_smv.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit-smv"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/project/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/formal/project_check_ok.smv"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.check.project.fail_node_input
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/check_fail_input/app/main.ahfl"
            "-DAHFLC_ARGS=check\;--search-root\;${AHFL_TESTS_DIR}/project/check_fail_input\;${AHFL_TESTS_DIR}/project/check_fail_input/app/main.ahfl"
            "-DEXPECTED_REGEX=type mismatch in workflow node input"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.project_manifest.fail_invalid
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/manifest_invalid/ahfl.project.json"
            "-DAHFLC_ARGS=check\;--project\;${AHFL_TESTS_DIR}/project/manifest_invalid/ahfl.project.json"
            "-DEXPECTED_REGEX=project descriptor requires non-empty 'search_roots'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.workspace.fail_missing_project
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/ahfl.workspace.json"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/project/ahfl.workspace.json\;--project-name\;missing"
            "-DEXPECTED_REGEX=workspace does not contain project named 'missing'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.project_manifest.fail_escape
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/manifest_escape/ahfl.project.json"
            "-DAHFLC_ARGS=check\;--project\;${AHFL_TESTS_DIR}/project/manifest_escape/ahfl.project.json"
            "-DEXPECTED_REGEX=descriptor field 'search_roots' must not escape the descriptor root"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.project_manifest.fail_package_without_native_json
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DAHFLC_ARGS=check\;--project\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json\;--package\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.package.json"
            "-DEXPECTED_REGEX=--package is only supported with emit-native-json, emit-execution-plan, emit-execution-journal, emit-replay-view, emit-audit-report, emit-scheduler-snapshot, emit-checkpoint-record, emit-checkpoint-review, emit-scheduler-review, emit-runtime-session, emit-dry-run-trace, emit-package-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.project_manifest.workflow_value_flow.fail_unknown_package_capability
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DAHFLC_ARGS=emit-native-json\;--project\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json\;--package\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.bad_capability.package.json"
            "-DEXPECTED_REGEX=unknown package authoring capability 'MissingCapability'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_execution_plan.project_manifest.workflow_value_flow.fail_agent_entry
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json"
            "-DAHFLC_ARGS=emit-execution-plan\;--project\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.project.json\;--package\;${AHFL_TESTS_DIR}/project/workflow_value_flow/ahfl.agent_entry.package.json"
            "-DEXPECTED_REGEX=package entry target 'lib::agents::AliasAgent' is not a workflow target for execution planner bootstrap"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.workspace.fail_duplicate_project_name
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workspace_duplicate/ahfl.workspace.json"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/project/workspace_duplicate/ahfl.workspace.json\;--project-name\;dup-project"
            "-DEXPECTED_REGEX=workspace contains duplicate project name 'dup-project'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

set(AHFL_WORKFLOW_VALUE_FLOW_MANIFEST "${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/ahfl.toml")
set(AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE "${AHFL_TESTS_DIR}/integration/workflow_value_flow/ahfl.workspace.toml")
set(AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target workflow --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_AGENT_ENTRY_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target agent-entry --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_BAD_CAPABILITY_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target bad-capability --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS "--workspace ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE} --package workflow-value-flow --target workflow --sysroot ${PROJECT_SOURCE_DIR}")

add_test(NAME ahfl.frontend.project.ok_basic
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            ok-basic
            "${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/ok"
)

add_test(NAME ahfl.frontend.project.ignores_stdlib_search_root_env
    COMMAND ${CMAKE_COMMAND} -E env
            "AHFL_STDLIB_SEARCH_ROOT=${CMAKE_BINARY_DIR}/missing-stdlib-root"
            $<TARGET_FILE:ahfl_project_parse_tests>
            ok-basic
            "${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/ok"
)

add_test(NAME ahfl.frontend.project.fail_missing
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-missing
            "${AHFL_TESTS_DIR}/integration/missing/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/missing"
)

add_test(NAME ahfl.frontend.project.fail_mismatch
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-mismatch
            "${AHFL_TESTS_DIR}/integration/mismatch/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/mismatch"
)

add_test(NAME ahfl.frontend.project.fail_no_module
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-no-module
            "${AHFL_TESTS_DIR}/integration/no_module/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/no_module"
)

add_test(NAME ahfl.frontend.project.fail_duplicate_owner
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            fail-duplicate-owner
            "${AHFL_TESTS_DIR}/integration/duplicate_owner/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/duplicate_owner"
)

add_test(NAME ahfl.frontend.project.package_dependency_gates_imports
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            package-dependency-gates-imports
            "${CMAKE_BINARY_DIR}/package_dependency_gates_imports"
)

add_test(NAME ahfl.frontend.project.ok_project_stdlib_root_wins_over_bundled_copy
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            ok-project-stdlib-root-wins-over-bundled-copy
            "${PROJECT_SOURCE_DIR}"
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
            "${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/ok"
)

add_test(NAME ahfl.resolver.project.ok_duplicate_locals
    COMMAND $<TARGET_FILE:ahfl_project_resolve_tests>
            ok-duplicate-locals
            "${AHFL_TESTS_DIR}/integration/duplicate_locals/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/duplicate_locals"
)

add_test(NAME ahfl.resolver.project.fail_unknown_type
    COMMAND $<TARGET_FILE:ahfl_project_resolve_tests>
            fail-unknown-type
            "${AHFL_TESTS_DIR}/integration/resolve_error/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/resolve_error"
)

add_test(NAME ahfl.check.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-cross-file
            "${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/check_ok"
)

add_test(NAME ahfl.check.project.fail_node_input
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            fail-node-input
            "${AHFL_TESTS_DIR}/integration/check_fail_input/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/check_fail_input"
)

add_test(NAME ahfl.check.project.fail_completed_state
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            fail-completed-state
            "${AHFL_TESTS_DIR}/integration/check_fail_state/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/check_fail_state"
)

add_test(NAME ahfl.check.project.ok_expression_type_isolated
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-expression-type-isolated
            "${AHFL_TESTS_DIR}/integration/expression_type_isolated/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/expression_type_isolated"
)

add_test(NAME ahfl.check.project.ok_stdlib_runtime_api
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-stdlib-runtime-api
            "${AHFL_TESTS_DIR}/integration/stdlib_api_smoke/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/stdlib_api_smoke"
)

add_test(NAME ahfl.check.project.ok_trait_runtime_dispatch
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            ok-trait-runtime-dispatch
            "${AHFL_TESTS_DIR}/integration/trait_runtime_smoke/app/main.ahfl"
            "${AHFL_TESTS_DIR}/integration/trait_runtime_smoke"
)

add_test(NAME ahfl.ir.identity_visitor
    COMMAND $<TARGET_FILE:ahfl_compiler_ir_tests>
)

add_test(NAME ahfl.handoff.package.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.validate_normalizes_display_names
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-package-normalizes-display-names
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.package_reader_summary.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            package-reader-summary-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.package_reader_summary.fail_missing_export
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            package-reader-summary-rejects-missing-export
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            execution-planner-bootstrap-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.fail_agent_entry
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            execution-planner-bootstrap-rejects-agent-entry
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_planner_bootstrap.fail_missing_dependency
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            execution-planner-bootstrap-rejects-missing-dependency
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_plan.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            execution-plan-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_plan.fail_agent_entry
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            execution-plan-rejects-agent-entry
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-execution-plan-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_fail_missing_entry_workflow
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-execution-plan-rejects-missing-entry-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.execution_plan.validate_fail_unknown_value_read
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-execution-plan-rejects-unknown-value-read
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.validate_rejects_wrong_kind
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-package-rejects-wrong-kind
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.validate_rejects_duplicate_normalized_targets
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-package-rejects-duplicate-normalized-targets
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.validate_rejects_unknown_capability
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            validate-package-rejects-unknown-capability
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.handoff.package.file_expr_temporal
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_tests>
            file-expr-temporal
            "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
)

add_test(NAME ahfl.dry_run.local.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.local.fail_missing_workflow
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-missing-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.mock_set.parse_ok
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            parse-capability-mock-set-ok
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.mock_set.parse_fail_duplicate_selector
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            parse-capability-mock-set-rejects-duplicate-selector
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.mock_set.parse_fail_duplicate_json_field
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            parse-capability-mock-set-rejects-duplicate-json-field
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.local.fail_missing_mock
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-missing-mock
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.dry_run.local.fail_unused_mock
    COMMAND $<TARGET_FILE:ahfl_dry_run_tests>
            local-dry-run-rejects-unused-mock
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            build-runtime-session-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.model.fail_missing_workflow
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            build-runtime-session-rejects-missing-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.model.fail_missing_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            build-runtime-session-rejects-missing-mock
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.model.partial_pending_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            build-runtime-session-partial-on-pending-mock
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.model.fail_node_failure
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            build-runtime-session-failed-on-node-failure
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.fail_incomplete_completed_workflow
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-rejects-incomplete-completed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.fail_missing_used_mock
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-rejects-missing-used-mock
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.partial_workflow_ok
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-accepts-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.failed_workflow_ok
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-accepts-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.runtime_session.validation.fail_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_session_tests>
            validate-runtime-session-rejects-failed-without-failure-summary
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            build-replay-view-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            build-replay-view-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            build-replay-view-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.model.fail_invalid_execution_journal
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            build-replay-view-rejects-invalid-execution-journal
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.model.fail_session_journal_mismatch
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            build-replay-view-rejects-session-journal-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.validation.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            validate-replay-view-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.validation.fail_missing_completed_progression
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            validate-replay-view-rejects-missing-completed-progression
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.replay_view.validation.fail_execution_order_index_mismatch
    COMMAND $<TARGET_FILE:ahfl_tooling_replay_view_tests>
            validate-replay-view-rejects-execution-order-index-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.audit_report.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.audit_report.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.audit_report.bootstrap.fail_trace_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-rejects-trace-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.audit_report.bootstrap.fail_trace_execution_order_mismatch
    COMMAND $<TARGET_FILE:ahfl_audit_report_tests>
            build-audit-report-rejects-trace-execution-order-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_snapshot.bootstrap.fail_replay_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-snapshot-rejects-replay-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_review.model.completed_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-completed
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_review.model.failed_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-failed
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.scheduler_review.model.partial_summary
    COMMAND $<TARGET_FILE:ahfl_scheduler_snapshot_tests>
            build-scheduler-decision-summary-partial
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_record.bootstrap.fail_snapshot_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-record-rejects-snapshot-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_review.model.completed_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-completed
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_review.model.failed_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-failed
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.checkpoint_review.model.partial_summary
    COMMAND $<TARGET_FILE:ahfl_checkpoint_record_tests>
            build-checkpoint-review-partial
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-blocked-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-terminal-failed-ok
)

add_test(NAME ahfl.persistence_descriptor.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-terminal-partial-ok
)

add_test(NAME ahfl.persistence_descriptor.model.fail_missing_planned_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-missing-planned-identity
)

add_test(NAME ahfl.persistence_descriptor.model.fail_non_prefix_cursor
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-non-prefix-cursor
)

add_test(NAME ahfl.persistence_descriptor.model.fail_export_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-export-ready-with-blocker
)

add_test(NAME ahfl.persistence_descriptor.model.fail_unsupported_checkpoint_record_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-unsupported-checkpoint-record-format
)

add_test(NAME ahfl.persistence_descriptor.model.fail_ready_from_blocked_checkpoint
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-ready-from-blocked-checkpoint
)

add_test(NAME ahfl.persistence_descriptor.model.fail_terminal_failed_export_ready
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-terminal-failed-export-ready
)

add_test(NAME ahfl.persistence_descriptor.model.fail_store_adjacent_blocked
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-descriptor-rejects-store-adjacent-blocked
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-descriptor-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-descriptor-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-descriptor-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.fail_invalid_checkpoint_record
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-descriptor-rejects-invalid-checkpoint-record
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_descriptor.bootstrap.fail_checkpoint_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-descriptor-rejects-checkpoint-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-review-ok
)

add_test(NAME ahfl.persistence_review.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            validate-persistence-review-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.persistence_review.model.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-review-rejects-invalid-descriptor
)

add_test(NAME ahfl.persistence_review.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-review-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-review-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_descriptor_tests>
            build-persistence-review-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-blocked-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-terminal-failed-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-terminal-partial-ok
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_missing_export_package_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-missing-export-package-identity
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_duplicate_artifact_name
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-duplicate-artifact-name
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-ready-with-blocker
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_ready_from_blocked_persistence
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-ready-from-blocked-persistence
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_store_import_adjacent_blocked
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-store-import-adjacent-blocked
)

add_test(NAME ahfl.persistence_export_manifest.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-manifest-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-manifest-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-manifest-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-manifest-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-manifest-rejects-invalid-descriptor
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_manifest.bootstrap.fail_descriptor_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-manifest-rejects-descriptor-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-review-ok
)

add_test(NAME ahfl.persistence_export_review.model.fail_unsupported_source_manifest_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            validate-persistence-export-review-rejects-unsupported-source-manifest-format
)

add_test(NAME ahfl.persistence_export_review.model.fail_invalid_manifest
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-review-rejects-invalid-manifest
)

add_test(NAME ahfl.persistence_export_review.model.project_workflow_value_flow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-review-project-workflow-value-flow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-review-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.persistence_export_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_pipeline_persistence_export_tests>
            build-persistence-export-review-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.fail_invalid_manifest
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-rejects-invalid-manifest
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_descriptor.bootstrap.fail_manifest_workflow_mismatch
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-descriptor-rejects-manifest-workflow-mismatch
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
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
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_review.model.failed_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-failed-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_review.model.partial_workflow
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-partial-workflow
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.store_import_review.model.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_store_import_tests>
            build-store-import-review-rejects-invalid-descriptor
            "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
)

add_test(NAME ahfl.durable_store_import_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_terminal_failed_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-terminal-failed-ok
)

add_test(NAME ahfl.durable_store_import_request.model.validate_terminal_partial_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-terminal-partial-ok
)

add_test(NAME ahfl.durable_store_import_request.model.fail_missing_request_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-missing-request-identity
)

add_test(NAME ahfl.durable_store_import_request.model.fail_duplicate_artifact_name
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-duplicate-artifact-name
)

add_test(NAME ahfl.durable_store_import_request.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_request.model.fail_unsupported_source_descriptor_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-unsupported-source-descriptor-format
)

add_test(NAME ahfl.durable_store_import_request.model.fail_adapter_contract_blocked
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-adapter-contract-blocked
)

add_test(NAME ahfl.durable_store_import_request.model.fail_terminal_failed_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-request-rejects-terminal-failed-without-failure-summary
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.ready_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-request-ready-descriptor
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.completed_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-request-completed-descriptor
)

add_test(NAME ahfl.durable_store_import_request.bootstrap.fail_invalid_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-request-rejects-invalid-descriptor
)

add_test(NAME ahfl.durable_store_import_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-review-ok
)

add_test(NAME ahfl.durable_store_import_review.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            validate-durable-store-import-review-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_review.model.ready_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-review-ready-request
)

add_test(NAME ahfl.durable_store_import_review.model.failed_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-review-failed-request
)

add_test(NAME ahfl.durable_store_import_review.model.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_tests>
            build-durable-store-import-review-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-blocked-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-deferred-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejected-ok
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_missing_decision_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-missing-decision-identity
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_accepted_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-accepted-with-blocker
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_adapter_decision_consumable_blocked
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-adapter-decision-consumable-blocked
)

add_test(NAME ahfl.durable_store_import_decision.model.fail_rejected_without_failure_summary
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-rejects-rejected-without-failure-summary
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_missing_receipt_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-missing-receipt-identity
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_unsupported_source_decision_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-unsupported-source-decision-format
)

add_test(NAME ahfl.durable_store_import_receipt.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.ready_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-ready-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.blocked_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-blocked-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.deferred_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-deferred-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.rejected_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-rejected-decision
)

add_test(NAME ahfl.durable_store_import_receipt.bootstrap.fail_invalid_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-rejects-invalid-decision
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_missing_request_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-missing-request-identity
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_unsupported_source_receipt_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-unsupported-source-receipt-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.model.fail_ready_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-request-rejects-ready-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-ready-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.blocked_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-blocked-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.deferred_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-deferred-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.rejected_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-rejected-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_request.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-request-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-review-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-ready-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.rejected_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-rejected-request-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_review.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-review-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-deferred-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejected-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_missing_response_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-missing-response-identity
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_unsupported_source_request_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-unsupported-source-request-format
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.model.fail_accepted_with_blocker
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-rejects-accepted-with-blocker
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-ready-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-persistence-response-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.ready_response
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-ready-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.rejected_response
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-rejected-response-ok
)

add_test(NAME ahfl.durable_store_import_receipt_persistence_response_review.bootstrap.fail_invalid_response
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-persistence-response-review-rejects-invalid-response
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-blocked-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_deferred_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-deferred-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejected-ok
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_missing_persistence_id
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-missing-persistence-id
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_non_mutating_accepted
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-non-mutating-accepted
)

add_test(NAME ahfl.durable_store_import_adapter_execution.model.fail_mutated_non_accepted
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-adapter-execution-rejects-mutated-non-accepted
)

add_test(NAME ahfl.durable_store_import_adapter_execution.bootstrap.ready_response
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-adapter-execution-ready-response
)

add_test(NAME ahfl.durable_store_import_adapter_execution.bootstrap.fail_invalid_response
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-adapter-execution-rejects-invalid-response
)

add_test(NAME ahfl.durable_store_import_recovery_preview.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-recovery-preview-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.ready_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-ready-execution-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.rejected_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-rejected-execution-ok
)

add_test(NAME ahfl.durable_store_import_recovery_preview.bootstrap.fail_invalid_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-recovery-preview-rejects-invalid-execution
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejected-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_adapter_config.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-adapter-config-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_adapter_config.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-adapter-config-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.fail_planned_without_provider_id
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejects-planned-without-provider-id
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.model.fail_not_planned_mutating_intent
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-attempt-rejects-not-planned-mutating-intent
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.bootstrap.ready_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-ready-execution
)

add_test(NAME ahfl.durable_store_import_provider_write_attempt.bootstrap.fail_invalid_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-attempt-rejects-invalid-execution
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-recovery-handoff-ok
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-recovery-handoff-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_recovery_handoff.bootstrap.fail_invalid_write_attempt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-recovery-handoff-rejects-invalid-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.validate_rejected_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejected-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.missing_profile_load_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-missing-profile-load-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.profile_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-profile-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_profile.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-profile-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_driver_profile.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-profile-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_sdk_invocation
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-sdk-invocation
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-bound-without-descriptor
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.model.fail_bound_without_profile_load
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-binding-rejects-bound-without-profile-load
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.bootstrap.ready_write_attempt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-ready-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_binding.bootstrap.fail_invalid_write_attempt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-binding-rejects-invalid-write-attempt
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-driver-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_driver_readiness.bootstrap.fail_invalid_binding_plan
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-driver-readiness-rejects-invalid-binding-plan
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.profile_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-profile-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_profile.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-profile-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_runtime_profile.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-profile-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.model.fail_ready_without_envelope
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-preflight-rejects-ready-without-envelope
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.bootstrap.ready_binding
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-ready-binding
)

add_test(NAME ahfl.durable_store_import_provider_runtime_preflight.bootstrap.fail_invalid_driver_binding
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-preflight-rejects-invalid-driver-binding
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-runtime-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_runtime_readiness.bootstrap.fail_invalid_preflight
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-runtime-readiness-rejects-invalid-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.policy_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-policy-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_policy.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-policy-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_policy.model.fail_provider_coordinates
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-policy-rejects-provider-coordinates
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.model.fail_ready_without_handoff
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-envelope-rejects-ready-without-handoff
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.bootstrap.ready_preflight
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-ready-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_envelope.bootstrap.fail_invalid_preflight
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-envelope-rejects-invalid-preflight
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-handoff-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-handoff-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_handoff_readiness.bootstrap.fail_invalid_envelope
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-handoff-readiness-rejects-invalid-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.policy_mismatch
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-policy-mismatch-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_policy.model.fail_secret_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-policy-rejects-secret-material
)

add_test(NAME ahfl.durable_store_import_provider_host_policy.model.fail_host_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-policy-rejects-host-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.model.fail_ready_without_descriptor
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-rejects-ready-without-descriptor
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.bootstrap.ready_envelope
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-ready-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution.bootstrap.fail_invalid_envelope
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-rejects-invalid-envelope
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-host-execution-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.unsupported_capability
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-readiness-unsupported-capability-ok
)

add_test(NAME ahfl.durable_store_import_provider_host_execution_readiness.bootstrap.fail_invalid_host_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-host-execution-readiness-rejects-invalid-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.model.fail_ready_without_identity
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-rejects-ready-without-identity
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.ready_host_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-ready-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt.bootstrap.fail_invalid_host_execution
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-rejects-invalid-host-execution
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-execution-receipt-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-review-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_execution_receipt_review.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-local-host-execution-receipt-review-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.validate_blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-request-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_request.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-request-ready-receipt
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-response-placeholder-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_response_placeholder.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-response-placeholder-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_readiness.bootstrap.fail_invalid_placeholder
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-readiness-rejects-invalid-placeholder
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-interface-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-adapter-interface-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_adapter_interface_review.bootstrap.fail_invalid_plan
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-sdk-adapter-interface-review-rejects-invalid-plan
)

add_test(NAME ahfl.durable_store_import_provider_config_load.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-load-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_load.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-load-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_config_load.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-config-load-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_snapshot.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-snapshot-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-config-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_config_readiness.bootstrap.fail_invalid_snapshot
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-config-readiness-rejects-invalid-snapshot
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.fail_forbidden_material
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-rejects-forbidden-material
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.model.fail_side_effects
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-request-rejects-side-effects
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_request.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-secret-resolver-request-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_resolver_response.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-resolver-response-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_policy_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-secret-policy-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_secret_policy_review.bootstrap.fail_invalid_placeholder
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-secret-policy-review-rejects-invalid-placeholder
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_request.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-request-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_request.model.fail_sandbox_escape
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-request-rejects-sandbox-escape
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_record.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-record-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_record.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            run-durable-store-import-provider-local-host-harness-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_host_harness_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-host-harness-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_plan.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-plan-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_plan.model.fail_forbidden_fields
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-plan-rejects-forbidden-fields
)

add_test(NAME ahfl.durable_store_import_provider_sdk_payload_audit.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-payload-audit-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_contract.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-mock-adapter-contract-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_execution.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            run-durable-store-import-provider-sdk-mock-adapter-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_sdk_mock_adapter_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-sdk-mock-adapter-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_filesystem_alpha_plan.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-filesystem-alpha-plan-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_filesystem_alpha_result.model.validate_dry_run_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-filesystem-alpha-result-dry-run-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_filesystem_alpha_result.integration.opt_in_write_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            run-durable-store-import-provider-local-filesystem-alpha-opt-in-write-ok
)

add_test(NAME ahfl.durable_store_import_provider_local_filesystem_alpha_readiness.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-local-filesystem-alpha-readiness-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_retry_decision.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-retry-decision-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_retry_decision.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-retry-decision-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_commit_receipt.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-commit-receipt-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_commit_receipt.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-commit-receipt-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_commit_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-commit-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_recovery_checkpoint.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-recovery-checkpoint-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_recovery.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-write-recovery-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_write_recovery_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-write-recovery-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_failure_taxonomy_report.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-failure-taxonomy-report-ok
)

add_test(NAME ahfl.durable_store_import_provider_failure_taxonomy.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-failure-taxonomy-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_failure_taxonomy_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-failure-taxonomy-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_execution_audit_event.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-execution-audit-event-ok
)

add_test(NAME ahfl.durable_store_import_provider_execution_audit_event.bootstrap.matrix_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-execution-audit-event-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_telemetry_summary.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-telemetry-summary-ok
)

add_test(NAME ahfl.durable_store_import_provider_operator_review_event.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-operator-review-event-ok
)

add_test(NAME ahfl.durable_store_import_provider_compatibility_test_manifest.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-compatibility-test-manifest-ok
)

add_test(NAME ahfl.durable_store_import_provider_fixture_matrix.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-fixture-matrix-ok
)

add_test(NAME ahfl.durable_store_import_provider_compatibility_report.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-compatibility-report-ok
)

add_test(NAME ahfl.durable_store_import_provider_compatibility_report.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-compatibility-report-blocked-ok
)

add_test(NAME ahfl.durable_store_import_provider_registry.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-registry-ok
)

add_test(NAME ahfl.durable_store_import_provider_selection_plan.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-selection-plan-ok
)

add_test(NAME ahfl.durable_store_import_provider_capability_negotiation_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-capability-negotiation-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_selection_plan.bootstrap.fallback_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-selection-fallback-ok
)

add_test(NAME ahfl.durable_store_import_provider_production_readiness_evidence.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-production-readiness-evidence-ok
)

add_test(NAME ahfl.durable_store_import_provider_production_readiness_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-production-readiness-review-ok
)

add_test(NAME ahfl.durable_store_import_provider_production_readiness_report.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-provider-production-readiness-report-ok
)

add_test(NAME ahfl.durable_store_import_provider_production_readiness.bootstrap.blocked_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-provider-production-readiness-blocked-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-review-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.model.fail_unsupported_source_receipt_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-receipt-review-rejects-unsupported-source-receipt-format
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.ready_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-ready-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.rejected_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-rejected-receipt-ok
)

add_test(NAME ahfl.durable_store_import_receipt_review.bootstrap.fail_invalid_receipt
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-receipt-review-rejects-invalid-receipt
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.ready_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-ready-request
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.completed_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-completed-request
)

add_test(NAME ahfl.durable_store_import_decision.bootstrap.fail_invalid_request
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-rejects-invalid-request
)

add_test(NAME ahfl.durable_store_import_decision_review.model.validate_ok
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-review-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.model.fail_unsupported_source_decision_format
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            validate-durable-store-import-decision-review-rejects-unsupported-source-decision-format
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.accepted_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-accepted-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.rejected_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-rejected-ok
)

add_test(NAME ahfl.durable_store_import_decision_review.bootstrap.fail_invalid_decision
    COMMAND $<TARGET_FILE:ahfl_pipeline_durable_store_import_decision_tests>
            build-durable-store-import-decision-review-rejects-invalid-decision
)

add_test(NAME ahfl.handoff.package_compat.normalize_identity_format_version
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_compat_tests>
            normalize-identity-format-version
)

add_test(NAME ahfl.handoff.package_compat.omit_empty_provenance
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_compat_tests>
            omit-empty-provenance
)

add_test(NAME ahfl.handoff.package_compat.escape_control_characters
    COMMAND $<TARGET_FILE:ahfl_compiler_handoff_package_compat_tests>
            escape-control-characters
)

add_test(NAME ahflc.dump_project.ok_basic
    COMMAND $<TARGET_FILE:ahflc> dump project
            --search-root "${AHFL_TESTS_DIR}/integration/ok"
            "${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
)
set_tests_properties(ahflc.dump_project.ok_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "source_graph \\(1 entry, 2 sources, 1 import\\)"
)

add_test(NAME ahflc.dump_package_graph.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> dump package-graph
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.dump_package_graph.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"name\":\"audit-core\""
)

add_test(NAME ahflc.dump_lockfile.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> dump lockfile
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.dump_lockfile.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"manifest\":\"ahfl.toml\""
)

add_test(NAME ahflc.check.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_library_target
    COMMAND $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target lib
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.manifest_library_target PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_requires_target_for_multi_target_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package 'refund-audit' contains 2 targets; pass --target <name>"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.manifest_uses_ahfl_sysroot_env
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/.."
            ${CMAKE_COMMAND} -E env
            --unset=AHFL_STDLIB_SEARCH_ROOT
            "AHFL_SYSROOT=${PROJECT_SOURCE_DIR}"
            $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
)
set_tests_properties(ahflc.check.manifest_uses_ahfl_sysroot_env PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_rejects_stdlib_search_root_env
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=${CMAKE_COMMAND}"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=-E\;chdir\;${PROJECT_SOURCE_DIR}/..\;${CMAKE_COMMAND}\;-E\;env\;--unset=AHFL_SYSROOT\;AHFL_STDLIB_SEARCH_ROOT=${PROJECT_SOURCE_DIR}\;$<TARGET_FILE:ahflc>\;check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--target\;workflow"
            "-DEXPECTED_REGEX=failed to locate sysroot std/ahfl\\.toml"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.manifest_requires_toml_extension
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=--manifest expects ahfl\\.toml"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.manifest_rejects_legacy_project_json
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.project.json"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.project.json\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=--manifest expects ahfl\\.toml"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> emit native-json
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.emit_native_json.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"name\": \"refund-audit\""
)

add_test(NAME ahflc.emit_package_review.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> emit package-review
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.emit_package_review.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "identity refund-audit@0\\.1\\.0"
)

add_test(NAME ahflc.emit_execution_plan.manifest_basic
    COMMAND $<TARGET_FILE:ahflc> emit execution-plan
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.emit_execution_plan.manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "refund_audit::main::RefundAuditWorkflow"
)

add_test(NAME ahflc.emit_dry_run_trace.manifest_requires_capability_mocks
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=emit\;dry-run-trace\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=emit-dry-run-trace requires --capability-mocks"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_provider_artifact.manifest_requires_capability_mocks
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=emit-provider-artifact\;provider/write-attempt\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=emit-provider-artifact provider/write-attempt requires --capability-mocks"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.manifest_rejects_workspace_package_selector
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=emit\;native-json\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--target\;workflow\;--package\;refund-audit\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=--manifest cannot be combined with --package"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.discover_manifest_basic
    COMMAND $<TARGET_FILE:ahflc> check
            "${AHFL_TESTS_DIR}/integration/package_graph_manifest/src/main.ahfl"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.discover_manifest_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_lockfile_drift_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest_lock_drift/ahfl.toml"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest_lock_drift/ahfl.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=field 'checksum'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.discover_manifest_lockfile_drift_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest_lock_drift/src/main.ahfl"
            "-DAHFLC_ARGS=check\;${AHFL_TESTS_DIR}/integration/package_graph_manifest_lock_drift/src/main.ahfl\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=field 'checksum'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.dump_package_graph.workspace_basic
    COMMAND $<TARGET_FILE:ahflc> dump package-graph
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.dump_package_graph.workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"source\":\"workspace\""
)

add_test(NAME ahflc.dump_lockfile.workspace_basic
    COMMAND $<TARGET_FILE:ahflc> dump lockfile
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.dump_lockfile.workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"manifest\":\"packages/refund-audit/ahfl.toml\""
)

add_test(NAME ahflc.check.workspace_basic
    COMMAND $<TARGET_FILE:ahflc> check
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 4 source\\(s\\)"
)

add_test(NAME ahflc.check.workspace_requires_target_for_multi_target_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml\;--package\;refund-audit\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package 'refund-audit' contains 3 targets; pass --target <name>"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.workspace_rejects_legacy_workspace_json
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.json"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.json\;--package\;refund-audit\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=--workspace expects ahfl\\.workspace\\.toml"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.workspace_basic
    COMMAND $<TARGET_FILE:ahflc> emit native-json
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.emit_native_json.workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "\"entry_target\": \\{"
)

add_test(NAME ahflc.emit_package_review.workspace_basic
    COMMAND $<TARGET_FILE:ahflc> emit package-review
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.emit_package_review.workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "identity refund-audit@0\\.1\\.0"
)

add_test(NAME ahflc.check.discover_workspace_basic
    COMMAND $<TARGET_FILE:ahflc> check
            "${AHFL_TESTS_DIR}/integration/package_graph_workspace/packages/refund-audit/src/main.ahfl"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.discover_workspace_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 4 source\\(s\\)"
)

add_test(NAME ahflc.check.workspace_private_import_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml\;--package\;refund-audit\;--target\;bad-private\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=imported module 'audit_core::internal' is private to package prefix 'audit_core'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.workspace_parent_export_private_child_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml\;--package\;refund-audit\;--target\;bad-private-child\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=imported module 'audit_core::lib::internal' is private to package prefix 'audit_core'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> check
            --search-root "${AHFL_TESTS_DIR}/integration/check_ok"
            "${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
)
set_tests_properties(ahflc.check.project.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
)

add_test(NAME ahflc.check.stdlib_api_smoke
    COMMAND $<TARGET_FILE:ahflc> check
            --search-root "${AHFL_TESTS_DIR}/integration/stdlib_api_smoke"
            "${AHFL_TESTS_DIR}/integration/stdlib_api_smoke/app/main.ahfl"
)
set_tests_properties(ahflc.check.stdlib_api_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
)

add_test(NAME ahflc.check.project_implicit_prelude_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/prelude_explicit/implicit_prelude.ahfl"
            "-DAHFLC_ARGS=check\;${AHFL_TESTS_DIR}/integration/prelude_explicit/implicit_prelude.ahfl\;--search-root\;${AHFL_TESTS_DIR}/integration/prelude_explicit"
            "-DEXPECTED_REGEX=unknown callable 'some'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.dump_types.project.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> dump types
            --search-root "${AHFL_TESTS_DIR}/integration/check_ok"
            "${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
)
set_tests_properties(ahflc.dump_types.project.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow app::main::MainWorkflow"
)

add_test(NAME ahflc.dump_project.search_root.ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> dump project
            --search-root "${AHFL_TESTS_DIR}/integration/check_ok"
            "${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
)
set_tests_properties(ahflc.dump_project.search_root.ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "source_graph \\(1 entry, 3 sources, 3 imports\\)"
)

add_test(NAME ahflc.dump_ast.search_root.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=dump ast --search-root ${AHFL_TESTS_DIR}/integration/check_ok ${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/integration/project_check_ok.ast"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_ir.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit ir"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/integration/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/ir/project_check_ok.ir"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_ir_json.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit ir-json"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/integration/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/ir/project_check_ok.json"
            "-DNORMALIZE_IDS=1"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_ir_json.search_root.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit ir-json --search-root ${AHFL_TESTS_DIR}/integration/workflow_value_flow ${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/ir/project_workflow_value_flow.json"
            "-DNORMALIZE_IDS=1"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.project.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit native-json"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/integration/workflow_value_flow"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/native/project_workflow_value_flow.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_native_json.package_requires_workspace
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DAHFLC_ARGS=emit\;native-json\;--package\;workflow-value-flow\;${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DEXPECTED_REGEX=--package is only supported with --workspace <ahfl\\.workspace\\.toml>"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_package_review.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit package-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/review/project_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_plan.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit execution-plan ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/plan/project_workflow_value_flow.with_package.execution-plan.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_dry_run_trace.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit dry-run-trace ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/trace/project_workflow_value_flow.with_package.dry-run-trace.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit runtime-session ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/session/project_workflow_value_flow.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit runtime-session ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/session/project_workflow_value_flow.failed.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_journal.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit execution-journal ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/journal/project_workflow_value_flow.with_package.execution-journal.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit replay-view ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/replay/project_workflow_value_flow.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit replay-view ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/replay/project_workflow_value_flow.failed.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.manifest.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit audit-report ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/audit/project_workflow_value_flow.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit audit-report ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/audit/project_workflow_value_flow.failed.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_snapshot.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit scheduler-snapshot ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/scheduler/project_workflow_value_flow.failed.with_package.scheduler-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_record.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit checkpoint-record ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/checkpoint/project_workflow_value_flow.failed.with_package.checkpoint-record.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit checkpoint-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/checkpoint/project_workflow_value_flow.failed.with_package.checkpoint-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_descriptor.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit persistence-descriptor ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/persistence/project_workflow_value_flow.failed.with_package.persistence-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit persistence-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/persistence/project_workflow_value_flow.failed.with_package.persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_manifest.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit export-manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/export/project_workflow_value_flow.failed.with_package.export-manifest.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit export-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/export/project_workflow_value_flow.failed.with_package.export-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_descriptor.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store-import-descriptor ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/store_import/project_workflow_value_flow.failed.with_package.store-import-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store-import-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/store_import/project_workflow_value_flow.failed.with_package.store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_request.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/request ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_decision.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/decision ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_request.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-request ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-response ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-response.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-response-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-receipt-persistence-response-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_adapter_execution.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/adapter-execution ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-adapter-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_recovery_preview.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/recovery-preview ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-recovery-preview"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_write_attempt.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/write-attempt ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-write-attempt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_recovery_handoff.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/recovery-handoff --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-recovery-handoff"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_binding.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/driver-binding ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-driver-binding.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/driver-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-driver-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_preflight.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/runtime-preflight --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-runtime-preflight.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/runtime-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-runtime-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_envelope.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-envelope --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-envelope.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-handoff-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-handoff-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/host-execution --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-host-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/host-execution-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-host-execution-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-execution-receipt --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-local-host-execution-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-execution-receipt-review --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-local-host-execution-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_request.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-request --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-response-placeholder --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-response-placeholder.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-interface --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-interface.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-interface-review --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-sdk-adapter-interface-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

function(ahfl_add_provider_v30_v33_project_cli_tests VARIANT_NAME AHFL_ARGS EXPECTED_PREFIX)
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_resolver_request.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/secret-resolver-request --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-resolver-request.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_resolver_response.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/secret-resolver-response --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-resolver-response.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_secret_policy_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/secret-policy-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-secret-policy-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_request.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-harness-request --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-request.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_record.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-harness-record --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-record.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_host_harness_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-harness-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-host-harness-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_payload_plan.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-payload-plan --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-payload-plan.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_payload_audit.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-payload-audit --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-payload-audit"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_contract.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-mock-adapter-contract --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-contract.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_execution.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-mock-adapter-execution ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-execution.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_sdk_mock_adapter_readiness.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-mock-adapter-readiness --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-sdk-mock-adapter-readiness"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
endfunction()

function(ahfl_add_provider_v34_v36_project_cli_tests VARIANT_NAME AHFL_ARGS EXPECTED_PREFIX)
    add_test(NAME ahflc.emit_durable_store_import_provider_local_filesystem_alpha_plan.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-filesystem-alpha-plan --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-filesystem-alpha-plan.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_filesystem_alpha_result.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-filesystem-alpha-result --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-filesystem-alpha-result.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_local_filesystem_alpha_readiness.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/local-filesystem-alpha-readiness --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-local-filesystem-alpha-readiness"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_retry_decision.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-retry-decision --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-retry-decision.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_commit_receipt.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-commit-receipt --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-commit-receipt.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_commit_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-commit-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-commit-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_recovery_checkpoint.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-recovery-checkpoint --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-recovery-checkpoint.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_recovery_plan.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-recovery-plan ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-recovery-plan.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_write_recovery_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/write-recovery-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-write-recovery-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_failure_taxonomy_report.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/failure-taxonomy-report ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-failure-taxonomy-report.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_failure_taxonomy_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/failure-taxonomy-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-failure-taxonomy-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_execution_audit_event.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/execution-audit-event --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-execution-audit-event.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_telemetry_summary.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/telemetry-summary --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-telemetry-summary.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_operator_review_event.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/operator-review-event --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-operator-review-event"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_compatibility_test_manifest.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/compatibility-test-manifest --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-compatibility-test-manifest.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_fixture_matrix.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/fixture-matrix --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-fixture-matrix.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_compatibility_report.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/compatibility-report ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-compatibility-report.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_registry.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/registry ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-registry.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_selection_plan.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/selection-plan ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-selection-plan.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_capability_negotiation_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/capability-negotiation-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-capability-negotiation-review"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_production_readiness_evidence.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/production-readiness-evidence ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-production-readiness-evidence.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_production_readiness_review.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/production-readiness-review --show-hidden ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-production-readiness-review.json"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
    add_test(NAME ahflc.emit_durable_store_import_provider_production_readiness_report.${VARIANT_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=emit-provider-artifact provider/production-readiness-report ${AHFL_ARGS}"
                "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/${EXPECTED_PREFIX}.durable-store-import-provider-production-readiness-report"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
endfunction()

add_test(NAME ahflc.emit_durable_store_import_provider_config_load.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-load --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-load.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_snapshot.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-snapshot --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_readiness.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-provider-config-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

ahfl_add_provider_v30_v33_project_cli_tests(
    manifest.workflow_value_flow.failed.with_package
    "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
    "project_workflow_value_flow.failed.with_package"
)

ahfl_add_provider_v34_v36_project_cli_tests(
    manifest.workflow_value_flow.failed.with_package
    "${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
    "project_workflow_value_flow.failed.with_package"
)

add_test(NAME ahflc.emit_durable_store_import_decision_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/decision-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.failed.with_package.durable-store-import-decision-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_review.manifest.workflow_value_flow.failed.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit scheduler-review ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.fail.mocks.json --input-fixture fixture.request.failed --run-id run-failed-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/scheduler/project_workflow_value_flow.failed.with_package.scheduler-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_package_review.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit package-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/review/project_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_plan.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit execution-plan ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/plan/project_workflow_value_flow.with_package.execution-plan.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_dry_run_trace.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit dry-run-trace ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/trace/project_workflow_value_flow.with_package.dry-run-trace.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit runtime-session ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/session/project_workflow_value_flow.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit runtime-session ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/session/project_workflow_value_flow.partial.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_journal.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit execution-journal ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/journal/project_workflow_value_flow.with_package.execution-journal.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit replay-view ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/replay/project_workflow_value_flow.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit replay-view ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/replay/project_workflow_value_flow.partial.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.workspace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit audit-report ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/audit/project_workflow_value_flow.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit audit-report ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/audit/project_workflow_value_flow.partial.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_snapshot.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit scheduler-snapshot ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/scheduler/project_workflow_value_flow.partial.with_package.scheduler-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_record.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit checkpoint-record ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/checkpoint/project_workflow_value_flow.partial.with_package.checkpoint-record.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit checkpoint-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/checkpoint/project_workflow_value_flow.partial.with_package.checkpoint-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_descriptor.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit persistence-descriptor ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/persistence/project_workflow_value_flow.partial.with_package.persistence-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_persistence_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit persistence-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/persistence/project_workflow_value_flow.partial.with_package.persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_manifest.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit export-manifest ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/export/project_workflow_value_flow.partial.with_package.export-manifest.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_export_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit export-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/export/project_workflow_value_flow.partial.with_package.export-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_descriptor.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store-import-descriptor ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/store_import/project_workflow_value_flow.partial.with_package.store-import-descriptor.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_store_import_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store-import-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/store_import/project_workflow_value_flow.partial.with_package.store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/request ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_decision.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/decision ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-request ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-response ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-response.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_receipt_persistence_response_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/receipt-persistence-response-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-receipt-persistence-response-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_adapter_execution.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/adapter-execution ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-adapter-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_recovery_preview.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/recovery-preview ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-recovery-preview"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_write_attempt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/write-attempt ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-write-attempt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_recovery_handoff.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/recovery-handoff --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-recovery-handoff"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_binding.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/driver-binding ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-driver-binding.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_driver_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/driver-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-driver-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_preflight.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/runtime-preflight --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-runtime-preflight.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_runtime_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/runtime-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-runtime-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_envelope.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-envelope --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-envelope.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_handoff_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-handoff-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-handoff-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/host-execution --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-host-execution.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_host_execution_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/host-execution-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-host-execution-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-execution-receipt --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-local-host-execution-receipt.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_local_host_execution_receipt_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/local-host-execution-receipt-review --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-local-host-execution-receipt-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_request.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-request --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-request.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_response_placeholder.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-response-placeholder --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-response-placeholder.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-interface --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-interface.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_sdk_adapter_interface_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/sdk-adapter-interface-review --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-sdk-adapter-interface-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_load.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-load --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-load.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_snapshot.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-snapshot --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_durable_store_import_provider_config_readiness.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-provider-artifact provider/config-readiness --show-hidden ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-provider-config-readiness"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

ahfl_add_provider_v30_v33_project_cli_tests(
    workspace.workflow_value_flow.partial.with_package
    "${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
    "project_workflow_value_flow.partial.with_package"
)

ahfl_add_provider_v34_v36_project_cli_tests(
    workspace.workflow_value_flow.partial.with_package
    "${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
    "project_workflow_value_flow.partial.with_package"
)

add_test(NAME ahflc.emit_durable_store_import_decision_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit store/decision-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/durable_store_import/project_workflow_value_flow.partial.with_package.durable-store-import-decision-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_review.workspace.workflow_value_flow.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit scheduler-review ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS} --capability-mocks ${AHFL_TESTS_DIR}/golden/dry_run/project_workflow_value_flow.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/scheduler/project_workflow_value_flow.partial.with_package.scheduler-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_summary.search_root.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit summary --search-root ${AHFL_TESTS_DIR}/integration/workflow_value_flow ${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/summary/project_workflow_value_flow.summary"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_smv.project.ok_cross_file
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit smv"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=--search-root ${AHFL_TESTS_DIR}/integration/check_ok"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formal/project_check_ok.smv"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.emit_smv.decreases.ok_decreases_length_self
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSUBCOMMAND=emit smv"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_decreases_length_self.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_decreases_length_self.smv"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
)

add_test(NAME ahflc.check.project.fail_node_input
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_fail_input/app/main.ahfl"
            "-DAHFLC_ARGS=check\;--search-root\;${AHFL_TESTS_DIR}/integration/check_fail_input\;${AHFL_TESTS_DIR}/integration/check_fail_input/app/main.ahfl"
            "-DEXPECTED_REGEX=exact schema mismatch in workflow node input"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_execution_plan.manifest.workflow_value_flow.fail_agent_entry
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}"
            "-DAHFLC_ARGS=emit\;execution-plan\;--manifest\;${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST}\;--target\;agent-entry\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package entry target 'lib::agents::AliasAgent' is not a workflow target for execution planner bootstrap"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

# --- v0.44 schema compatibility tests ---

add_test(NAME ahfl.schema_compatibility.model.test_all_compatible
    COMMAND $<TARGET_FILE:ahfl_schema_compatibility_tests>
            test-all-compatible
)

add_test(NAME ahfl.schema_compatibility.model.test_incompatible_version
    COMMAND $<TARGET_FILE:ahfl_schema_compatibility_tests>
            test-incompatible-version
)

add_test(NAME ahfl.schema_compatibility.model.test_source_chain_incompatible
    COMMAND $<TARGET_FILE:ahfl_schema_compatibility_tests>
            test-source-chain-incompatible
)

add_test(NAME ahfl.schema_compatibility.model.test_empty_identity_fields
    COMMAND $<TARGET_FILE:ahfl_schema_compatibility_tests>
            test-empty-identity-fields
)

add_test(NAME ahfl.schema_compatibility.model.test_validation_wrong_format_version
    COMMAND $<TARGET_FILE:ahfl_schema_compatibility_tests>
            test-validation-wrong-format-version
)

# --- v0.45 config bundle validation tests ---

add_test(NAME ahfl.config_bundle_validation.model.test_build_validation_report
    COMMAND $<TARGET_FILE:ahfl_config_bundle_validation_tests>
            test-build-validation-report
)

add_test(NAME ahfl.config_bundle_validation.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_config_bundle_validation_tests>
            test-validate-format-version
)

add_test(NAME ahfl.config_bundle_validation.model.test_validate_security_constraints
    COMMAND $<TARGET_FILE:ahfl_config_bundle_validation_tests>
            test-validate-security-constraints
)

add_test(NAME ahfl.config_bundle_validation.model.test_summary_counts
    COMMAND $<TARGET_FILE:ahfl_config_bundle_validation_tests>
            test-summary-counts
)

add_test(NAME ahfl.release_evidence_archive.model.test_build_manifest
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-build-manifest
)

add_test(NAME ahfl.release_evidence_archive.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-validate-format-version
)

add_test(NAME ahfl.release_evidence_archive.model.test_validate_empty_fields
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-validate-empty-fields
)

add_test(NAME ahfl.release_evidence_archive.model.test_build_with_invalid_conformance
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-build-with-invalid-conformance
)

add_test(NAME ahfl.release_evidence_archive.model.test_invalid_conformance_evidence
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-invalid-conformance-evidence
)

add_test(NAME ahfl.release_evidence_archive.model.test_count_consistency
    COMMAND $<TARGET_FILE:ahfl_release_evidence_archive_tests>
            test-count-consistency
)

add_test(NAME ahfl.approval_workflow.model.test_build_approval_request
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-build-approval-request
)

add_test(NAME ahfl.approval_workflow.model.test_build_receipt_rejected
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-build-receipt-rejected
)

add_test(NAME ahfl.approval_workflow.model.test_build_receipt_approved
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-build-receipt-approved
)

add_test(NAME ahfl.approval_workflow.model.test_build_receipt_deferred
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-build-receipt-deferred
)

add_test(NAME ahfl.approval_workflow.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-validate-format-version
)

add_test(NAME ahfl.approval_workflow.model.test_validate_rejection_details
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-validate-rejection-details
)

add_test(NAME ahfl.approval_workflow.model.test_validate_receipt_consistency
    COMMAND $<TARGET_FILE:ahfl_approval_workflow_tests>
            test-validate-receipt-consistency
)

add_test(NAME ahfl.opt_in_guard.model.test_build_all_gates_pass
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-build-all-gates-pass
)

add_test(NAME ahfl.opt_in_guard.model.test_build_deny_no_approval
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-build-deny-no-approval
)

add_test(NAME ahfl.opt_in_guard.model.test_build_deny_config_invalid
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-build-deny-config-invalid
)

add_test(NAME ahfl.opt_in_guard.model.test_build_deny_registry_mismatch
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-build-deny-registry-mismatch
)

add_test(NAME ahfl.opt_in_guard.model.test_build_deny_security_constraints
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-build-deny-security-constraints
)

add_test(NAME ahfl.opt_in_guard.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-validate-format-version
)

add_test(NAME ahfl.opt_in_guard.model.test_validate_decision_consistency
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-validate-decision-consistency
)

add_test(NAME ahfl.opt_in_guard.model.test_default_deny
    COMMAND $<TARGET_FILE:ahfl_opt_in_guard_tests>
            test-default-deny
)

add_test(NAME ahfl.runtime_policy.model.test_build_all_gates_pass
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-all-gates-pass
)

add_test(NAME ahfl.runtime_policy.model.test_build_deny_opt_in_not_granted
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-deny-opt-in-not-granted
)

add_test(NAME ahfl.runtime_policy.model.test_build_deny_approval_missing
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-deny-approval-missing
)

add_test(NAME ahfl.runtime_policy.model.test_build_deny_config_invalid
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-deny-config-invalid
)

add_test(NAME ahfl.runtime_policy.model.test_build_deny_registry_mismatch
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-deny-registry-mismatch
)

add_test(NAME ahfl.runtime_policy.model.test_build_deny_readiness_not_met
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-build-deny-readiness-not-met
)

add_test(NAME ahfl.runtime_policy.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-validate-format-version
)

add_test(NAME ahfl.runtime_policy.model.test_validate_decision_consistency
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-validate-decision-consistency
)

add_test(NAME ahfl.runtime_policy.model.test_default_deny
    COMMAND $<TARGET_FILE:ahfl_runtime_engine_policy_tests>
            test-default-deny
)

add_test(NAME ahfl.production_integration.model.test_build_all_evidence_pass
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-build-all-evidence-pass
)

add_test(NAME ahfl.production_integration.model.test_build_blocked_conformance_fails
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-build-blocked-conformance-fails
)

add_test(NAME ahfl.production_integration.model.test_build_blocked_schema_incompatible
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-build-blocked-schema-incompatible
)

add_test(NAME ahfl.production_integration.model.test_build_blocked_approval_rejected
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-build-blocked-approval-rejected
)

add_test(NAME ahfl.production_integration.model.test_build_blocked_runtime_policy_deny
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-build-blocked-runtime-policy-deny
)

add_test(NAME ahfl.production_integration.model.test_validate_format_version
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-validate-format-version
)

add_test(NAME ahfl.production_integration.model.test_validate_readiness_consistency
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-validate-readiness-consistency
)

add_test(NAME ahfl.production_integration.model.test_default_safe_values
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-default-safe-values
)

add_test(NAME ahfl.production_integration.model.test_validate_non_mutating_mode
    COMMAND $<TARGET_FILE:ahfl_production_integration_tests>
            test-validate-non-mutating-mode
)

add_test(NAME ahfl.evaluator.eval_all
    COMMAND $<TARGET_FILE:ahfl_runtime_evaluator_tests>
)

add_test(NAME ahfl.evaluator.p7_runtime_all
    COMMAND $<TARGET_FILE:ahfl_runtime_evaluator_p7_tests>
)

add_test(NAME ahfl.evaluator.generics_all
    COMMAND $<TARGET_FILE:ahfl_runtime_evaluator_generics_tests>
)

add_test(NAME ahfl.executor.exec_all
    COMMAND $<TARGET_FILE:ahfl_executor_tests>
)

add_test(NAME ahfl.runtime.agent_runtime_all
    COMMAND $<TARGET_FILE:ahfl_agent_runtime_tests>
)

add_test(NAME ahfl.runtime.workflow_runtime_all
    COMMAND $<TARGET_FILE:ahfl_workflow_runtime_tests>
)

add_test(NAME ahfl.runtime.capability_bridge_all
    COMMAND $<TARGET_FILE:ahfl_capability_bridge_tests>
)

add_test(NAME ahfl.runtime.response_schema_validator
    COMMAND $<TARGET_FILE:ahfl_response_schema_validator_tests>
)

add_test(NAME ahfl.runtime.e2e_workflow
    COMMAND $<TARGET_FILE:ahfl_e2e_workflow_tests>
            "${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
)

add_test(NAME ahfl.llm_provider.all
    COMMAND $<TARGET_FILE:ahfl_runtime_provider_llm_tests>
)

add_test(NAME ahfl.runtime.http_transport_all
    COMMAND $<TARGET_FILE:ahfl_http_transport_tests>
)

add_test(NAME ahfl.runtime.grpc_transport_all
    COMMAND $<TARGET_FILE:ahfl_grpc_transport_tests>
)

add_test(NAME ahfl.json.value_all
    COMMAND $<TARGET_FILE:ahfl_base_json_value_tests>
)

add_test(NAME ahfl.toml.syntax_all
    COMMAND $<TARGET_FILE:ahfl_base_toml_tests>
)

add_test(NAME ahfl.manifest.schema_all
    COMMAND $<TARGET_FILE:ahfl_compiler_manifest_tests>
)

add_test(NAME ahfl.package_graph.core_all
    COMMAND $<TARGET_FILE:ahfl_compiler_package_graph_tests>
)

add_test(NAME ahfl.runtime.value_json_all
    COMMAND $<TARGET_FILE:ahfl_value_json_tests>
)

add_test(NAME ahfl.secret.provider_all
    COMMAND $<TARGET_FILE:ahfl_runtime_provider_secret_provider_tests>
)

add_test(NAME ahfl.secret.vault_rotation_all
    COMMAND $<TARGET_FILE:ahfl_vault_rotation_tests>
)

add_test(NAME ahfl.passes.pass_manager_all
    COMMAND $<TARGET_FILE:ahfl_pass_manager_tests>
)

add_test(NAME ahfl.semantics.type_relations_all
    COMMAND $<TARGET_FILE:ahfl_semantics_type_relations_tests>
)

add_test(NAME ahfl.semantics.type_resolver_all
    COMMAND $<TARGET_FILE:ahfl_semantics_type_resolver_tests>
)

add_test(NAME ahfl.semantics.typed_hir_all
    COMMAND $<TARGET_FILE:ahfl_semantics_typed_hir_tests>
)

add_test(NAME ahfl.semantics.effects_all
    COMMAND $<TARGET_FILE:ahfl_semantics_effects_tests>
)

add_test(NAME ahfl.semantics.diagnostic_matrix_all
    COMMAND $<TARGET_FILE:ahfl_semantics_diagnostic_matrix_tests>
)

add_test(NAME ahfl.semantics.type_mismatch_origin_all
    COMMAND $<TARGET_FILE:ahfl_semantics_type_mismatch_origin_tests>
)

add_test(NAME ahfl.semantics.stmt_diagnostics_all
    COMMAND $<TARGET_FILE:ahfl_semantics_stmt_diagnostics_tests>
)

add_test(NAME ahfl.semantics.const_sema_negatives_all
    COMMAND $<TARGET_FILE:ahfl_semantics_const_sema_negatives_tests>
)

add_test(NAME ahfl.semantics.flow_condition_all
    COMMAND $<TARGET_FILE:ahfl_semantics_flow_condition_tests>
)

add_test(NAME ahfl.syntax.trait_impl_all
    COMMAND $<TARGET_FILE:ahfl_syntax_trait_impl_tests>
)

add_test(NAME ahfl.semantics.adt_match_all
    COMMAND $<TARGET_FILE:ahfl_semantics_adt_match_tests>
)

# P2 fn_generics_closures: re-enabled after grammar fixes (-> return type,
# 'effect' keyword). Lambda typecheck and Fn() type still limited.
add_test(NAME ahfl.semantics.fn_generics_closures_all
    COMMAND $<TARGET_FILE:ahfl_semantics_fn_generics_closures_tests>
)

# P3 trait/impl: trait declaration + impl block typecheck, signature matching,
# and the strict orphan rule (RFC §2.2) via a multi-module SourceGraph.
add_test(NAME ahfl.semantics.trait_impl_all
    COMMAND $<TARGET_FILE:ahfl_semantics_trait_impl_tests>
)

add_test(NAME ahfl.semantics.validate_plumbing_all
    COMMAND $<TARGET_FILE:ahfl_semantics_validate_plumbing_tests>
)

add_test(NAME ahfl.semantics.where_clause_info_all
    COMMAND $<TARGET_FILE:ahfl_semantics_where_clause_info_tests>
)

add_test(NAME ahfl.semantics.monomorphization_all
    COMMAND $<TARGET_FILE:ahfl_semantics_monomorphization_tests>
)

add_test(NAME ahfl.assurance.obligations_all
    COMMAND $<TARGET_FILE:ahfl_assurance_obligations_tests>
)

add_test(NAME ahfl.llm_provider.streaming_all
    COMMAND $<TARGET_FILE:ahfl_streaming_tests>
)

add_test(NAME ahfl.lsp.json_rpc_all
    COMMAND $<TARGET_FILE:ahfl_tooling_lsp_json_rpc_tests>
)

add_test(NAME ahfl.lsp.handler_all
    COMMAND $<TARGET_FILE:ahfl_tooling_lsp_handler_tests>
)

add_test(NAME ahfl.lsp.process_smoke
    COMMAND ${Python3_EXECUTABLE} "${AHFL_TESTS_DIR}/scripts/lsp_smoke.py" $<TARGET_FILE:ahfl-lsp>
)

add_test(NAME ahfl.runtime.connection_pool_all
    COMMAND $<TARGET_FILE:ahfl_connection_pool_tests>
)

add_test(NAME ahfl.frontend.error_recovery_all
    COMMAND $<TARGET_FILE:ahfl_error_recovery_tests>
)

# Wave-21 A-1: parser recursion-depth guard (PARSER_STACK_OVERFLOW + 4 categories × 3 variants)
add_test(NAME ahfl.frontend.parser_stack_depth_all
    COMMAND $<TARGET_FILE:ahfl_parser_stack_depth_tests>
)

# DecreasesClauseSyntax (R-09: not dispatched through DeclKind).
add_test(NAME ahfl.frontend.decreases_structure_all
    COMMAND $<TARGET_FILE:ahfl_decreases_structure_tests>
)
add_test(NAME ahfl.frontend.decreases_printer_all
    COMMAND $<TARGET_FILE:ahfl_decreases_printer_tests>
)
add_test(NAME ahfl.frontend.decreases_desugar_all
    COMMAND $<TARGET_FILE:ahfl_decreases_desugar_tests>
)
add_test(NAME ahfl.frontend.decreases_symmetry_all
    COMMAND $<TARGET_FILE:ahfl_decreases_symmetry_tests>
)

# Wave-19 Lane 3b F1 — RFC d-1 Enum variant named fields (struct variant)
# minimal grammar POC. Coverage: parse → AST → ast_printer → formatter
# roundtrip. Intentionally stops BEFORE resolver / typechecker / TypedHIR / IR.
add_test(NAME ahfl.frontend.enum_struct_variant_all
    COMMAND $<TARGET_FILE:ahfl_enum_struct_variant_tests>
)

# Wave-19 Lane 3b F2 — RFC e-1 Optional narrowing `if let Some(x) = expr`
# minimal syntax POC. Coverage: parse → AST → ast_printer → formatter
# roundtrip. Narrowing semantics / TypedHIR lowering / exhaustive-pattern
# checks are intentionally NOT implemented (pre-approval demo).
add_test(NAME ahfl.frontend.if_let_syntax_poc_all
    COMMAND $<TARGET_FILE:ahfl_if_let_syntax_poc_tests>
)

add_test(NAME ahfl.support.thread_pool_all
    COMMAND $<TARGET_FILE:ahfl_thread_pool_tests>
)

add_test(NAME ahfl.support.sha256_all
    COMMAND $<TARGET_FILE:ahfl_sha256_tests>
)

add_test(NAME ahfl.support.version_all
    COMMAND $<TARGET_FILE:ahfl_version_tests>
)

add_test(NAME ahfl.support.diagnostic_serialization_all
    COMMAND $<TARGET_FILE:ahfl_base_diagnostic_serialization_tests>
)

add_test(NAME ahfl.support.decreases_diagnostics_all
    COMMAND $<TARGET_FILE:ahfl_base_decreases_diagnostics_tests>
)
add_test(NAME ahfl.support.trait_impl_diagnostics_all
    COMMAND $<TARGET_FILE:ahfl_base_trait_impl_diagnostics_tests>
)
add_test(NAME ahfl.support.diagnostics_code_smoke_all
    COMMAND $<TARGET_FILE:ahfl_base_diagnostics_code_smoke_tests>
)

add_test(NAME ahfl.formal.bmc_all
    COMMAND $<TARGET_FILE:ahfl_bmc_tests>
)

add_test(NAME ahfl.formal.model_checker_backends_all
    COMMAND $<TARGET_FILE:ahfl_model_checker_tests>
)

add_test(NAME ahfl.formal.integration_improvement_all
    COMMAND $<TARGET_FILE:ahfl_verification_formal_integration_tests>
)

add_test(NAME ahfl.formal.counterexample_parse_all
    COMMAND $<TARGET_FILE:ahfl_counterexample_parse_tests>
)

add_test(NAME ahfl.formal.bmc_depth_customization_all
    COMMAND $<TARGET_FILE:ahfl_bmc_depth_customization_tests>
)

add_test(NAME ahfl.runtime.parallel_scheduler_all
    COMMAND $<TARGET_FILE:ahfl_parallel_scheduler_tests>
)

add_test(NAME ahfl.runtime.sandbox_all
    COMMAND $<TARGET_FILE:ahfl_sandbox_tests>
)

add_test(NAME ahfl.runtime.distributed_all
    COMMAND $<TARGET_FILE:ahfl_distributed_tests>
)

add_test(NAME ahfl.formatter.formatter_all
    COMMAND $<TARGET_FILE:ahfl_tooling_formatter_tests>
)

add_test(NAME ahfl.repl.repl_all
    COMMAND $<TARGET_FILE:ahfl_tooling_repl_tests>
)

add_test(NAME ahfl.repl.process_smoke
    COMMAND ${Python3_EXECUTABLE} "${AHFL_TESTS_DIR}/scripts/repl_smoke.py" $<TARGET_FILE:ahfl-repl>
)

add_test(NAME ahfl.dap.basic_all
    COMMAND $<TARGET_FILE:ahfl_tooling_dap_tests>
)

add_test(NAME ahfl.dap.process_smoke
    COMMAND ${Python3_EXECUTABLE} "${AHFL_TESTS_DIR}/scripts/dap_smoke.py" $<TARGET_FILE:ahfl-dap>
)

add_test(NAME ahfl.telemetry.telemetry_all
    COMMAND $<TARGET_FILE:ahfl_tooling_telemetry_tests>
)

add_test(NAME ahfl.profiling.profiling_all
    COMMAND $<TARGET_FILE:ahfl_tooling_profiling_tests>
)

add_test(NAME ahfl.abi.compat_all
    COMMAND $<TARGET_FILE:ahfl_tooling_abi_tests>
)

add_test(NAME ahfl.incremental.incremental_all
    COMMAND $<TARGET_FILE:ahfl_tooling_incremental_tests>
)

add_test(NAME ahfl.incremental.process_smoke
    COMMAND ${Python3_EXECUTABLE} "${AHFL_TESTS_DIR}/scripts/incremental_smoke.py" $<TARGET_FILE:ahfl-incremental> "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_test(NAME ahfl.backends.wasm_all
        COMMAND $<TARGET_FILE:ahfl_wasm_backend_tests>
    )
endif()

add_test(NAME ahfl.backends.registry_all
    COMMAND $<TARGET_FILE:ahfl_compiler_backends_registry_tests>
)

add_test(NAME ahfl.cli.command_routing_all
    COMMAND $<TARGET_FILE:ahfl_cli_command_routing_tests>
)

if(AHFL_ENABLE_BACKEND_INFRA)
    add_test(NAME ahfl.backends.targets_all
        COMMAND $<TARGET_FILE:ahfl_target_backends_tests>
    )
endif()

add_test(NAME ahfl.package.package_all
    COMMAND $<TARGET_FILE:ahfl_tooling_package_tests>
)

add_test(NAME ahfl.property.lowering_equiv
    COMMAND $<TARGET_FILE:ahfl_property_lowering_tests>
)

add_test(NAME ahfl.property.smv_syntax
    COMMAND $<TARGET_FILE:ahfl_property_smv_tests>
)

add_test(NAME ahfl.ir.opt.lower_and_passes
    COMMAND $<TARGET_FILE:ahfl_compiler_ir_opt_tests>
)

add_test(NAME ahfl.semantics.decreases_recognizer_all
    COMMAND $<TARGET_FILE:ahfl_semantics_decreases_recognizer_tests>
)

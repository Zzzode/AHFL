ahfl_add_check_test(
    ahflc.check.example
    "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)

add_test(NAME ahflc.dump_ast.example
    COMMAND $<TARGET_FILE:ahflc> dump-ast
            "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_ast.example PROPERTIES
    PASS_REGULAR_EXPRESSION "program (.*/)?examples/refund_audit_core_v0_1\\.ahfl"
)

add_test(NAME ahflc.dump_types.example
    COMMAND $<TARGET_FILE:ahflc> dump-types
            "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_types.example PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow refund::audit::RefundAuditWorkflow"
)

ahfl_add_output_test(
    ahflc.emit_ir.example
    emit-ir
    "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
    "${AHFL_TESTS_DIR}/ir/refund_audit_core_v0_1.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.alias_const
    emit-ir
    "${AHFL_TESTS_DIR}/ir/ok_alias_const.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_alias_const.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.expr_temporal
    emit-ir
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.flow_workflow_semantics
    emit-ir
    "${AHFL_TESTS_DIR}/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_flow_workflow_semantics.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.workflow_value_flow
    emit-ir
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ir"
)

ahfl_add_output_test(
    ahflc.emit_summary.workflow_value_flow
    emit-summary
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/summary/ok_workflow_value_flow.summary"
)

ahfl_add_output_test(
    ahflc.emit_ir_json.expr_temporal
    emit-ir-json
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.json"
)

ahfl_add_output_test(
    ahflc.emit_smv.alias_const
    emit-smv
    "${AHFL_TESTS_DIR}/ir/ok_alias_const.ahfl"
    "${AHFL_TESTS_DIR}/formal/ok_alias_const.smv"
)

ahfl_add_output_test(
    ahflc.emit_smv.expr_temporal
    emit-smv
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/formal/ok_expr_temporal.smv"
)

ahfl_add_output_test(
    ahflc.emit_smv.flow_workflow_semantics
    emit-smv
    "${AHFL_TESTS_DIR}/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/formal/ok_flow_workflow_semantics.smv"
)

ahfl_add_output_test(
    ahflc.emit_ir_json.flow_workflow_semantics
    emit-ir-json
    "${AHFL_TESTS_DIR}/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/formal/ok_flow_workflow_semantics.json"
)

ahfl_add_output_test(
    ahflc.emit_ir_json.workflow_value_flow
    emit-ir-json
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.json"
)

ahfl_add_output_test(
    ahflc.emit_native_json.workflow_value_flow
    emit-native-json
    "${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/native/ok_workflow_value_flow.native.json"
)

add_test(NAME ahflc.emit_native_json.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/ok_workflow_value_flow.with_package.native.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_package_review.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-package-review --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/review/ok_workflow_value_flow.with_package.review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_plan.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-plan --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/plan/ok_workflow_value_flow.with_package.execution-plan.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_dry_run_trace.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-dry-run-trace --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/trace/ok_workflow_value_flow.with_package.dry-run-trace.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/ok_workflow_value_flow.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_runtime_session.alias_const.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-runtime-session --package ${AHFL_TESTS_DIR}/ir/ok_alias_const.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_alias_const.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001 ${AHFL_TESTS_DIR}/ir/ok_alias_const.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/session/ok_alias_const.partial.with_package.runtime-session.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.alias_const.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --package ${AHFL_TESTS_DIR}/ir/ok_alias_const.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_alias_const.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001 ${AHFL_TESTS_DIR}/ir/ok_alias_const.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/ok_alias_const.partial.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.alias_const.partial.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --package ${AHFL_TESTS_DIR}/ir/ok_alias_const.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_alias_const.pending.mocks.json --input-fixture fixture.request.partial --run-id run-partial-001 ${AHFL_TESTS_DIR}/ir/ok_alias_const.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/ok_alias_const.partial.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_execution_journal.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-execution-journal --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/journal/ok_workflow_value_flow.with_package.execution-journal.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_replay_view.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-replay-view --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/replay/ok_workflow_value_flow.with_package.replay-view.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_audit_report.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-audit-report --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/audit/ok_workflow_value_flow.with_package.audit-report.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_snapshot.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-snapshot --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/ok_workflow_value_flow.with_package.scheduler-snapshot.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_record.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-record --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/ok_workflow_value_flow.with_package.checkpoint-record.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_checkpoint_review.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-checkpoint-review --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/checkpoint/ok_workflow_value_flow.with_package.checkpoint-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.emit_scheduler_review.workflow_value_flow.with_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-scheduler-review --package ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.package.json --capability-mocks ${AHFL_TESTS_DIR}/dry_run/ok_workflow_value_flow.mocks.json --input-fixture fixture.request.basic --run-id run-001 ${AHFL_TESTS_DIR}/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/scheduler/ok_workflow_value_flow.with_package.scheduler-review"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

ahfl_add_output_test(
    ahflc.emit_native_json.expr_temporal
    emit-native-json
    "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/native/ok_expr_temporal.native.json"
)

ahfl_add_check_test(
    ahflc.check.import_alias
    "${AHFL_TESTS_DIR}/resolver/ok_import_alias_self.ahfl"
)

add_test(NAME ahflc.dump_types.alias_schema
    COMMAND $<TARGET_FILE:ahflc> dump-types
            "${AHFL_TESTS_DIR}/typecheck/ok_agent_schema_alias.ahfl"
)
set_tests_properties(ahflc.dump_types.alias_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "input: types::alias::Request"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_type
    "${AHFL_TESTS_DIR}/resolver/duplicate_top_level_type.ahfl"
    "duplicate type"
)

ahfl_add_check_fail_test(
    ahflc.fail.unknown_references
    "${AHFL_TESTS_DIR}/resolver/unknown_references.ahfl"
    "unknown type"
)

ahfl_add_check_fail_test(
    ahflc.fail.type_alias_cycle
    "${AHFL_TESTS_DIR}/resolver/type_alias_cycle.ahfl"
    "type alias cycle detected"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_import_alias
    "${AHFL_TESTS_DIR}/resolver/duplicate_import_alias.ahfl"
    "duplicate import alias"
)

ahfl_add_check_fail_test(
    ahflc.fail.ambiguous_callable
    "${AHFL_TESTS_DIR}/resolver/ambiguous_callable.ahfl"
    "ambiguous callable"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_schema_not_struct
    "${AHFL_TESTS_DIR}/typecheck/agent_schema_not_struct.ahfl"
    "agent input type must resolve to a struct type"
)

ahfl_add_check_fail_test(
    ahflc.fail.if_condition_not_bool
    "${AHFL_TESTS_DIR}/typecheck/if_condition_not_bool.ahfl"
    "if condition must have type Bool"
)

ahfl_add_check_fail_test(
    ahflc.fail.assert_condition_not_bool
    "${AHFL_TESTS_DIR}/typecheck/assert_condition_not_bool.ahfl"
    "assert condition must have type Bool"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_capability_call
    "${AHFL_TESTS_DIR}/typecheck/contract_capability_call.ahfl"
    "capability call 'Validate' is not allowed in this context"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_unknown_state
    "${AHFL_TESTS_DIR}/typecheck/contract_invariant_unknown_state.ahfl"
    "unknown state 'Missing'"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_running_not_allowed
    "${AHFL_TESTS_DIR}/typecheck/contract_invariant_running_not_allowed.ahfl"
    "running\\(\\.\\.\\.\\) is only valid in workflow safety/liveness formulas"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_output_not_allowed
    "${AHFL_TESTS_DIR}/typecheck/contract_invariant_output_not_allowed.ahfl"
    "unknown value 'output'"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_unreachable_state
    "${AHFL_TESTS_DIR}/typecheck/agent_unreachable_state.ahfl"
    "state 'Dead' is unreachable"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_missing_handler
    "${AHFL_TESTS_DIR}/typecheck/flow_missing_handler.ahfl"
    "missing non-final-state handler for 'Middle'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_illegal_goto
    "${AHFL_TESTS_DIR}/typecheck/flow_illegal_goto.ahfl"
    "illegal goto from state 'Init' to 'Init'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_return_in_non_final
    "${AHFL_TESTS_DIR}/typecheck/flow_return_in_non_final.ahfl"
    "return is only allowed in final state handlers"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_after_missing
    "${AHFL_TESTS_DIR}/typecheck/workflow_after_missing.ahfl"
    "unknown workflow dependency 'missing'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_dependency_cycle
    "${AHFL_TESTS_DIR}/typecheck/workflow_dependency_cycle.ahfl"
    "workflow dependency cycle detected"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_dependency_not_visible
    "${AHFL_TESTS_DIR}/typecheck/workflow_dependency_not_visible.ahfl"
    "unknown value 'first'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_completed_invalid_state
    "${AHFL_TESTS_DIR}/typecheck/workflow_completed_invalid_state.ahfl"
    "state 'Init' is not a final state of node 'run'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_node_input_mismatch
    "${AHFL_TESTS_DIR}/typecheck/workflow_node_input_mismatch.ahfl"
    "type mismatch in workflow node input"
)

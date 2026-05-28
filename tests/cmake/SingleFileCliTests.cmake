ahfl_add_check_test(
    ahflc.check.example
    "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)

add_test(NAME ahflc.dump_ast.example
    COMMAND $<TARGET_FILE:ahflc> dump ast
            "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_ast.example PROPERTIES
    PASS_REGULAR_EXPRESSION "program (.*/)?examples/refund_audit_core_v0_1\\.ahfl"
)

add_test(NAME ahflc.dump_types.example
    COMMAND $<TARGET_FILE:ahflc> dump types
            "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_types.example PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow refund::audit::RefundAuditWorkflow"
)

ahfl_add_output_test(
    ahflc.emit_ir.example
    "emit ir"
    "${AHFL_EXAMPLES_DIR}/refund_audit_core_v0_1.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/refund_audit_core_v0_1.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.alias_const
    "emit ir"
    "${AHFL_TESTS_DIR}/golden/ir/ok_alias_const.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_alias_const.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.expr_temporal
    "emit ir"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.flow_workflow_semantics
    "emit ir"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.ir"
)

ahfl_add_output_test(
    ahflc.emit_ir.workflow_value_flow
    "emit ir"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ir"
)

ahfl_add_output_test(
    ahflc.emit_summary.workflow_value_flow
    "emit summary"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/golden/summary/ok_workflow_value_flow.summary"
)

ahfl_add_output_test(
    ahflc.emit_assurance_json.effects
    "emit assurance-json"
    "${AHFL_TESTS_DIR}/golden/assurance/ok_effects.ahfl"
    "${AHFL_TESTS_DIR}/golden/assurance/ok_effects.assurance.json"
)

add_test(NAME ahflc.validate_assurance.effects
    COMMAND $<TARGET_FILE:ahflc> validate
            "${AHFL_TESTS_DIR}/golden/assurance/ok_effects.ahfl"
)
set_tests_properties(ahflc.validate_assurance.effects PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: assurance validation ready"
)

ahfl_add_command_fail_test(
    ahflc.validate_assurance.fail_missing_effect
    validate
    "${AHFL_TESTS_DIR}/golden/assurance/fail_missing_effect.ahfl"
    "missing_effect_spec"
)

ahfl_add_command_fail_test(
    ahflc.validate_assurance.fail_financial_missing_controls
    validate
    "${AHFL_TESTS_DIR}/golden/assurance/fail_financial_missing_controls.ahfl"
    "missing_financial_compensation"
)

ahfl_add_command_fail_test(
    ahflc.validate_assurance.fail_external_missing_audit
    validate
    "${AHFL_TESTS_DIR}/golden/assurance/fail_external_missing_audit.ahfl"
    "audit_event"
)

ahfl_add_command_fail_test(
    ahflc.validate_assurance.fail_retry_idempotency
    validate
    "${AHFL_TESTS_DIR}/golden/assurance/fail_retry_idempotency.ahfl"
    "retry_safe_if_idempotent_without_key"
)

ahfl_add_output_test(
    ahflc.emit_ir_json.expr_temporal
    "emit ir-json"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.json"
)

ahfl_add_output_test(
    ahflc.emit_smv.alias_const
    "emit smv"
    "${AHFL_TESTS_DIR}/golden/ir/ok_alias_const.ahfl"
    "${AHFL_TESTS_DIR}/golden/formal/ok_alias_const.smv"
)

ahfl_add_output_test(
    ahflc.emit_smv.expr_temporal
    "emit smv"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/golden/formal/ok_expr_temporal.smv"
)

ahfl_add_output_test(
    ahflc.emit_smv.flow_workflow_semantics
    "emit smv"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.smv"
)

ahfl_add_output_test(
    ahflc.emit_smv.bounded_data_semantics
    "emit smv"
    "${AHFL_TESTS_DIR}/golden/formal/ok_bounded_data_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/formal/ok_bounded_data_semantics.smv"
)

ahfl_add_check_test(
    ahflc.check.formal_real_smv_control
    "${AHFL_TESTS_DIR}/golden/formal/ok_real_smv_control.ahfl"
)

ahfl_add_check_test(
    ahflc.check.formal_bounded_data_semantics
    "${AHFL_TESTS_DIR}/golden/formal/ok_bounded_data_semantics.ahfl"
)

add_test(NAME ahflc.verify_formal.fake_pass
    COMMAND $<TARGET_FILE:ahflc> verify
            --model-checker "${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_pass.sh"
            --formal-model-out
            "${CMAKE_CURRENT_BINARY_DIR}/formal/ok_flow_workflow_semantics.verify.smv"
            "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
)
set_tests_properties(ahflc.verify_formal.fake_pass PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: formal verification passed"
)

add_test(NAME ahflc.verify_formal.fake_fail
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--model-checker;${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_fail.sh;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=formal verification failed"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

if(AHFL_SMV_CHECKER)
    add_test(NAME ahflc.verify_formal.real_smv
        COMMAND $<TARGET_FILE:ahflc> verify
                --model-checker "${AHFL_SMV_CHECKER}"
                --formal-model-out
                "${CMAKE_CURRENT_BINARY_DIR}/formal/ok_real_smv_control.verify.smv"
                "${AHFL_TESTS_DIR}/golden/formal/ok_real_smv_control.ahfl"
    )
    set_tests_properties(ahflc.verify_formal.real_smv PROPERTIES
        PASS_REGULAR_EXPRESSION "ok: formal verification passed"
    )

    add_test(NAME ahflc.verify_formal.real_smv_observation_assumptions
        COMMAND $<TARGET_FILE:ahflc> verify
                --model-checker "${AHFL_SMV_CHECKER}"
                --formal-model-out
                "${CMAKE_CURRENT_BINARY_DIR}/formal/ok_expr_temporal.verify.smv"
                "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
    )
    set_tests_properties(ahflc.verify_formal.real_smv_observation_assumptions PROPERTIES
        PASS_REGULAR_EXPRESSION "ok: formal verification passed"
    )

    add_test(NAME ahflc.verify_formal.real_smv_effect_events
        COMMAND $<TARGET_FILE:ahflc> verify
                --model-checker "${AHFL_SMV_CHECKER}"
                --formal-model-out
                "${CMAKE_CURRENT_BINARY_DIR}/formal/ok_effects.verify.smv"
                "${AHFL_TESTS_DIR}/golden/assurance/ok_effects.ahfl"
    )
    set_tests_properties(ahflc.verify_formal.real_smv_effect_events PROPERTIES
        PASS_REGULAR_EXPRESSION "ok: formal verification passed"
    )

    add_test(NAME ahflc.verify_formal.real_smv_bounded_data_semantics
        COMMAND $<TARGET_FILE:ahflc> verify
                --model-checker "${AHFL_SMV_CHECKER}"
                --formal-model-out
                "${CMAKE_CURRENT_BINARY_DIR}/formal/ok_bounded_data_semantics.verify.smv"
                "${AHFL_TESTS_DIR}/golden/formal/ok_bounded_data_semantics.ahfl"
    )
    set_tests_properties(ahflc.verify_formal.real_smv_bounded_data_semantics PROPERTIES
        PASS_REGULAR_EXPRESSION "ok: formal verification passed"
    )

    add_test(NAME ahflc.verify_formal.real_smv_counterexample
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=verify;--model-checker;${AHFL_SMV_CHECKER};--formal-model-out;${CMAKE_CURRENT_BINARY_DIR}/formal/fail_real_smv_control.verify.smv;${AHFL_TESTS_DIR}/golden/formal/fail_real_smv_control.ahfl"
                "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/fail_real_smv_control.ahfl"
                "-DEXPECTED_REGEX=counterexample AHFL mapping"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
    )

    add_test(NAME ahflc.verify_formal.real_smv_bounded_data_counterexample
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=verify;--model-checker;${AHFL_SMV_CHECKER};--formal-model-out;${CMAKE_CURRENT_BINARY_DIR}/formal/fail_bounded_data_semantics.verify.smv;${AHFL_TESTS_DIR}/golden/formal/fail_bounded_data_semantics.ahfl"
                "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/fail_bounded_data_semantics.ahfl"
                "-DEXPECTED_REGEX=formal verification failed"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
    )
endif()

ahfl_add_output_test(
    ahflc.emit_ir_json.flow_workflow_semantics
    "emit ir-json"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.json"
)

ahfl_add_output_test(
    ahflc.emit_ir_json.workflow_value_flow
    "emit ir-json"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.json"
)

ahfl_add_output_test(
    ahflc.emit_native_json.workflow_value_flow
    "emit native-json"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
    "${AHFL_TESTS_DIR}/golden/native/ok_workflow_value_flow.native.json"
)

# Auto-discover and register all with_package golden tests.
# This replaces ~88 manual add_test() calls by scanning tests/golden/ for
# files matching the *.with_package.* naming convention.
ahfl_discover_package_golden_tests()

ahfl_add_output_test(
    ahflc.emit_native_json.expr_temporal
    "emit native-json"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ahfl"
    "${AHFL_TESTS_DIR}/golden/native/ok_expr_temporal.native.json"
)

ahfl_add_check_test(
    ahflc.check.import_alias
    "${AHFL_TESTS_DIR}/golden/resolver/ok_import_alias_self.ahfl"
)

add_test(NAME ahflc.dump_types.alias_schema
    COMMAND $<TARGET_FILE:ahflc> dump types
            "${AHFL_TESTS_DIR}/golden/typecheck/ok_agent_schema_alias.ahfl"
)
set_tests_properties(ahflc.dump_types.alias_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "input: types::alias::Request"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_type
    "${AHFL_TESTS_DIR}/golden/resolver/duplicate_top_level_type.ahfl"
    "duplicate type"
)

ahfl_add_check_fail_test(
    ahflc.fail.unknown_references
    "${AHFL_TESTS_DIR}/golden/resolver/unknown_references.ahfl"
    "unknown type"
)

ahfl_add_check_fail_test(
    ahflc.fail.type_alias_cycle
    "${AHFL_TESTS_DIR}/golden/resolver/type_alias_cycle.ahfl"
    "type alias cycle detected"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_import_alias
    "${AHFL_TESTS_DIR}/golden/resolver/duplicate_import_alias.ahfl"
    "duplicate import alias"
)

ahfl_add_check_fail_test(
    ahflc.fail.ambiguous_callable
    "${AHFL_TESTS_DIR}/golden/resolver/ambiguous_callable.ahfl"
    "ambiguous callable"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_schema_not_struct
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_schema_not_struct.ahfl"
    "agent input type must resolve to a struct type"
)

ahfl_add_check_fail_test(
    ahflc.fail.if_condition_not_bool
    "${AHFL_TESTS_DIR}/golden/typecheck/if_condition_not_bool.ahfl"
    "if condition must have type Bool"
)

ahfl_add_check_fail_test(
    ahflc.fail.assert_condition_not_bool
    "${AHFL_TESTS_DIR}/golden/typecheck/assert_condition_not_bool.ahfl"
    "assert condition must have type Bool"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_capability_call
    "${AHFL_TESTS_DIR}/golden/typecheck/contract_capability_call.ahfl"
    "capability call 'Validate' is not allowed in this context"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_unknown_state
    "${AHFL_TESTS_DIR}/golden/typecheck/contract_invariant_unknown_state.ahfl"
    "unknown state 'Missing'"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_running_not_allowed
    "${AHFL_TESTS_DIR}/golden/typecheck/contract_invariant_running_not_allowed.ahfl"
    "running\\(\\.\\.\\.\\) is only valid in workflow safety/liveness formulas"
)

ahfl_add_check_fail_test(
    ahflc.fail.contract_invariant_output_not_allowed
    "${AHFL_TESTS_DIR}/golden/typecheck/contract_invariant_output_not_allowed.ahfl"
    "unknown value 'output'"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_unreachable_state
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_unreachable_state.ahfl"
    "state 'Dead' is unreachable"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_missing_handler
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_missing_handler.ahfl"
    "missing non-final-state handler for 'Middle'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_illegal_goto
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_illegal_goto.ahfl"
    "illegal goto from state 'Init' to 'Init'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_return_in_non_final
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_return_in_non_final.ahfl"
    "return is only allowed in final state handlers"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_after_missing
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_after_missing.ahfl"
    "unknown workflow dependency 'missing'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_dependency_cycle
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_dependency_cycle.ahfl"
    "workflow dependency cycle detected"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_dependency_not_visible
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_dependency_not_visible.ahfl"
    "unknown value 'first'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_completed_invalid_state
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_completed_invalid_state.ahfl"
    "state 'Init' is not a final state of node 'run'"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_node_input_mismatch
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_node_input_mismatch.ahfl"
    "type mismatch in workflow node input"
)

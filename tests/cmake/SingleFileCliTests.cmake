set(AHFL_STDLIB_UNITS_MANIFEST "${AHFL_TESTS_DIR}/integration/stdlib_units/ahfl.toml")
set(AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST
    "${AHFL_TESTS_DIR}/integration/package_golden/container_variance_failures/ahfl.toml"
)
set(AHFL_IMPORT_ALIAS_MANIFEST
    "${AHFL_TESTS_DIR}/integration/package_golden/import_alias/ahfl.toml"
)
set(AHFL_REFUND_AUDIT_EXAMPLE_SOURCE "${AHFL_EXAMPLES_DIR}/refund/audit.ahfl")

ahfl_add_check_test(
    ahflc.check.example
    "${AHFL_REFUND_AUDIT_EXAMPLE_SOURCE}"
)

add_test(NAME ahflc.dump_ast.example
    COMMAND $<TARGET_FILE:ahflc> dump ast
            "${AHFL_REFUND_AUDIT_EXAMPLE_SOURCE}"
)
set_tests_properties(ahflc.dump_ast.example PROPERTIES
    PASS_REGULAR_EXPRESSION "program (.*/)?examples/refund/audit\\.ahfl"
)

add_test(NAME ahflc.dump_types.example
    COMMAND $<TARGET_FILE:ahflc> dump types
            "${AHFL_REFUND_AUDIT_EXAMPLE_SOURCE}"
)
set_tests_properties(ahflc.dump_types.example PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow refund::audit::RefundAuditWorkflow"
)

add_test(NAME ahflc.fmt.formats_file_with_config
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=format"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_2spaces.ahfl"
            "-DCONFIG_FILE=${AHFL_TESTS_DIR}/golden/formatter/two_spaces.ahfl-format"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/formats-file-with-config"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.check_pass
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=check-pass"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_4spaces.ahfl"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/check-pass"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.check_fail
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=check-fail"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DEXPECTED_REGEX=formatting check failed"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/check-fail"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.formats_directory
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=format-directory"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_2spaces.ahfl"
            "-DCONFIG_FILE=${AHFL_TESTS_DIR}/golden/formatter/two_spaces.ahfl-format"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/formats-directory"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.check_directory_fail
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=check-directory-fail"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DCONFIG_FILE=${AHFL_TESTS_DIR}/golden/formatter/two_spaces.ahfl-format"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/check-directory-fail"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.formats_manifest
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=format-manifest"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_2spaces.ahfl"
            "-DCONFIG_FILE=${AHFL_TESTS_DIR}/golden/formatter/two_spaces.ahfl-format"
            "-DSYSROOT_DIR=${PROJECT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/formats-manifest"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.fmt.formats_package_graph_workspace
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DMODE=format-workspace-manifest"
            "-DSOURCE_FILE=${AHFL_TESTS_DIR}/golden/formatter/unformatted_struct.ahfl"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_2spaces.ahfl"
            "-DCONFIG_FILE=${AHFL_TESTS_DIR}/golden/formatter/two_spaces.ahfl-format"
            "-DSYSROOT_DIR=${PROJECT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter/formats-package-graph-workspace"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunFormatterCliTest.cmake"
)

add_test(NAME ahflc.profile.time_passes.emit_summary
    COMMAND $<TARGET_FILE:ahflc> emit summary -O --time-passes
            "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
)
set_tests_properties(ahflc.profile.time_passes.emit_summary PROPERTIES
    PASS_REGULAR_EXPRESSION "pass timings:"
)

add_test(NAME ahflc.profile.time_passes.requires_optimize
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit;summary;--time-passes;${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_REGEX=--time-passes requires -O or --optimize"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.profile.smv_size_report.emit_smv
    COMMAND $<TARGET_FILE:ahflc> emit smv --smv-size-report
            "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
)
set_tests_properties(ahflc.profile.smv_size_report.emit_smv PROPERTIES
    PASS_REGULAR_EXPRESSION "smv size report:.*bytes: [0-9]+.*lines: [0-9]+.*ltlspecs: [0-9]+"
)

add_test(NAME ahflc.profile.smv_size_report.rejects_non_smv
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit;summary;--smv-size-report;${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
            "-DEXPECTED_REGEX=--smv-size-report is only supported with emit smv"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.profile.observability_exports
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ahfl"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/profile/observability-exports"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunCliObservabilityTest.cmake"
)

add_test(NAME ahflc.passes.semantic_backend_effect
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_pass_productization.ahfl"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunPassProductizationTest.cmake"
)

add_test(NAME ahflc.passes.workflow_simplification_backend_effect
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_workflow_simplification.ahfl"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunWorkflowSimplificationTest.cmake"
)

add_test(NAME ahflc.emit_public_api.package_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/public_api_artifact_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/public-api-smoke"
)

add_test(NAME ahflc.init_single_file.package_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/init_single_file_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/init-single-file-smoke"
)

add_test(NAME ahflc.package_archive.package_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/package_archive_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/package-archive-smoke"
)

add_test(NAME ahflc.package_publish.package_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/package_publish_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/package-publish-smoke"
)

add_test(NAME ahflc.package_registry_resolve.package_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/package_registry_resolve_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/package-registry-resolve-smoke"
)

add_test(NAME ahflc.quality.smv_size_budget.flow_workflow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DMAX_BYTES=22000"
            "-DMAX_LINES=170"
            "-DMAX_LTLSPEC=18"
            "-DMIN_LTLSPEC=10"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunSmvSizeBudgetTest.cmake"
)

add_test(NAME ahflc.quality.smv_size_budget.pass_productization
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_pass_productization.ahfl"
            "-DMAX_BYTES=20000"
            "-DMAX_LINES=150"
            "-DMAX_LTLSPEC=18"
            "-DMIN_LTLSPEC=10"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunSmvSizeBudgetTest.cmake"
)

add_test(NAME ahflc.quality.smv_size_budget.workflow_simplification
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_workflow_simplification.ahfl"
            "-DMAX_BYTES=48000"
            "-DMAX_LINES=280"
            "-DMAX_LTLSPEC=40"
            "-DMIN_LTLSPEC=25"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunSmvSizeBudgetTest.cmake"
)

add_test(NAME ahflc.quality.smv_size_budget.refund_audit
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_REFUND_AUDIT_EXAMPLE_SOURCE}"
            "-DMAX_BYTES=32000"
            "-DMAX_LINES=230"
            "-DMAX_LTLSPEC=32"
            "-DMIN_LTLSPEC=20"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunSmvSizeBudgetTest.cmake"
)

ahfl_add_output_test(
    ahflc.emit_ir.example
    "emit ir"
    "${AHFL_REFUND_AUDIT_EXAMPLE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/refund_audit.ir"
)

ahfl_add_package_output_test(
    ahflc.emit_ir.alias_const
    "emit ir"
    "ok_alias_const"
    "${AHFL_TESTS_DIR}/golden/ir/ok_alias_const.ir"
    PACKAGE_ONLY
)

ahfl_add_output_test(
    ahflc.emit_ir.expr_temporal
    "emit ir"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.ir"
)

ahfl_add_package_output_test(
    ahflc.emit_ir.flow_workflow_semantics
    "emit ir"
    "ok_flow_workflow_semantics"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.ir"
    PACKAGE_ONLY
)

ahfl_add_package_output_test(
    ahflc.emit_ir.workflow_value_flow
    "emit ir"
    "ok_workflow_value_flow"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.ir"
    PACKAGE_ONLY
)

ahfl_add_package_output_test(
    ahflc.emit_summary.workflow_value_flow
    "emit summary"
    "ok_workflow_value_flow"
    "${AHFL_TESTS_DIR}/golden/summary/ok_workflow_value_flow.summary"
    PACKAGE_ONLY
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

set(AHFL_RUN_SECRET_HANDLE_INPUT [[{"_type":"runtime::e2e_multi_agent::SupportRequest","user_id":"u-1","message":"hello","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"Low"}}]])
set(AHFL_RUN_SCHEMA_MISMATCH_INPUT [[{"_type":"runtime::e2e_multi_agent::SupportRequest","user_id":"u-1","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"Low"}}]])

add_test(NAME ahflc.run.llm_config.fail_missing_api_key_secret
    COMMAND ${CMAKE_COMMAND} -E env --unset=AHFL_TEST_MISSING_LLM_API_KEY_DO_NOT_SET
            ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SECRET_HANDLE_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_missing_secret.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=failed to resolve LLM api_key_secret 'AHFL_TEST_MISSING_LLM_API_KEY_DO_NOT_SET'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.run.llm_config.fail_invalid_budget
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SECRET_HANDLE_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_invalid_budget.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=max_prompt_tokens \\+ max_tokens must not exceed max_total_tokens"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.run.llm_config.fail_missing_fallback_api_key_secret
    COMMAND ${CMAKE_COMMAND} -E env --unset=AHFL_TEST_MISSING_FALLBACK_LLM_API_KEY_DO_NOT_SET
            ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SECRET_HANDLE_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_missing_fallback_secret.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=failed to resolve LLM fallback provider 'backup' api_key_secret 'AHFL_TEST_MISSING_FALLBACK_LLM_API_KEY_DO_NOT_SET'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.run.llm_config.fail_missing_vault_token_env
    COMMAND ${CMAKE_COMMAND} -E env --unset=AHFL_TEST_MISSING_VAULT_TOKEN_DO_NOT_SET
            ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SECRET_HANDLE_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_missing_vault_token.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=failed to resolve Vault token_env 'AHFL_TEST_MISSING_VAULT_TOKEN_DO_NOT_SET'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.run.llm_tools.fail_invalid_capability_mocks
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SECRET_HANDLE_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_test_key.json;--capability-mocks;${AHFL_TESTS_DIR}/golden/runtime/llm_tool_mocks_invalid.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=capability mock must specify exactly one of 'capability_name' or 'binding_key'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.run.llm_provider_runtime.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/llm_provider_runtime_smoke.py"
            $<TARGET_FILE:ahflc>
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/llm-provider-runtime-smoke"
)

add_test(NAME ahflc.run.profile_and_output_contract.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/run_profile_smoke.py"
            $<TARGET_FILE:ahflc>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/run-profile-smoke"
            "${PROJECT_SOURCE_DIR}/build/release-evidence/beta/run-profiles.json"
)

add_test(NAME ahflc.run.llm_failure_matrix.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/llm_failure_matrix_smoke.py"
            $<TARGET_FILE:ahflc>
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/llm-failure-matrix-smoke"
)

add_test(NAME ahflc.run.llm_secret_manager.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/llm_secret_manager_smoke.py"
            $<TARGET_FILE:ahflc>
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/llm-secret-manager-smoke"
)

add_test(NAME ahflc.run.capability_bindings.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/runtime_capability_bindings_smoke.py"
            $<TARGET_FILE:ahflc>
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/capability-bindings-smoke"
)

add_test(NAME ahflc.run.input_schema.fail_missing_field
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=run;--workflow;runtime::e2e_multi_agent::CustomerSupportWorkflow;--input;${AHFL_RUN_SCHEMA_MISMATCH_INPUT};--llm-config;${AHFL_TESTS_DIR}/golden/runtime/llm_config_test_key.json;${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/runtime/e2e_multi_agent.ahfl"
            "-DEXPECTED_REGEX=--input does not match workflow input schema"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
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
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.json"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir.expr_temporal
    "emit opt-ir"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.opt-ir"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_json.expr_temporal
    "emit opt-ir-json"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.opt-ir.json"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_optimized.expr_temporal
    "emit opt-ir -O"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.opt-ir.optimized"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_json_optimized.expr_temporal
    "emit opt-ir-json -O"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/ir/ok_expr_temporal.opt-ir.optimized.json"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir.flow_workflow_semantics
    "emit opt-ir"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.opt-ir"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_json.flow_workflow_semantics
    "emit opt-ir-json"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.opt-ir.json"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_optimized.flow_workflow_semantics
    "emit opt-ir -O"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.opt-ir.optimized"
)

ahfl_add_output_test(
    ahflc.emit_opt_ir_json_optimized.flow_workflow_semantics
    "emit opt-ir-json -O"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
    "${AHFL_TESTS_DIR}/golden/ir/ok_flow_workflow_semantics.opt-ir.optimized.json"
)

ahfl_add_package_output_test(
    ahflc.emit_smv.alias_const
    "emit smv"
    "ok_alias_const"
    "${AHFL_TESTS_DIR}/golden/formal/ok_alias_const.smv"
    PACKAGE_ONLY
)

ahfl_add_output_test(
    ahflc.emit_smv.expr_temporal
    "emit smv"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/formal/ok_expr_temporal.smv"
)

ahfl_add_package_output_test(
    ahflc.emit_smv.flow_workflow_semantics
    "emit smv"
    "ok_flow_workflow_semantics"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.smv"
    PACKAGE_ONLY
)

ahfl_add_package_output_test(
    ahflc.emit_smv.bounded_data_semantics
    "emit smv"
    "ok_bounded_data_semantics"
    "${AHFL_TESTS_DIR}/golden/formal/ok_bounded_data_semantics.smv"
    PACKAGE_ONLY
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

add_test(NAME ahflc.verify_formal.state_space_report
    COMMAND $<TARGET_FILE:ahflc> verify
            --model-checker "${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_pass.sh"
            "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
)
set_tests_properties(ahflc.verify_formal.state_space_report PROPERTIES
    PASS_REGULAR_EXPRESSION "state_space_estimate: 4"
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/formal/no-checker-path")

add_test(NAME ahflc.verify_formal.missing_binary
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--formal-backend;nuxmv;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=checker_status: missing_binary"
            "-DUNSET_ENV=AHFL_SMV_CHECKER;AHFL_NUXMV_PATH;AHFL_NUSMV_PATH"
            "-DENV_PATH=${CMAKE_CURRENT_BINARY_DIR}/formal/no-checker-path"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.verify_formal.unsupported_backend
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--formal-backend;spin;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=checker_status: verification_unsupported"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.verify_formal.checker_error
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--model-checker;${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_error.sh;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=checker_status: checker_error"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.verify_formal.checker_timeout
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--model-checker;${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_timeout.sh;--checker-timeout-seconds;1;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=checker_timed_out: true"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)
set_tests_properties(ahflc.verify_formal.checker_timeout PROPERTIES
    TIMEOUT 8
)

add_test(NAME ahflc.verify_formal.fake_fail
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--model-checker;${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_fail.sh;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=formal verification failed"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.verify_formal.fake_fail_explain
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=verify;--explain;--model-checker;${AHFL_TESTS_DIR}/golden/formal/fake_smv_checker_fail.sh;${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.ahfl"
            "-DEXPECTED_REGEX=counterexample explanation"
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
                "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
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

ahfl_add_package_output_test(
    ahflc.emit_ir_json.flow_workflow_semantics
    "emit ir-json"
    "ok_flow_workflow_semantics"
    "${AHFL_TESTS_DIR}/golden/formal/ok_flow_workflow_semantics.json"
    PACKAGE_ONLY
)

ahfl_add_package_output_test(
    ahflc.emit_ir_json.workflow_value_flow
    "emit ir-json"
    "ok_workflow_value_flow"
    "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_value_flow.json"
    PACKAGE_ONLY
)

ahfl_add_package_output_test(
    ahflc.emit_native_json.workflow_value_flow
    "emit native-json"
    "ok_workflow_value_flow"
    "${AHFL_TESTS_DIR}/golden/native/ok_workflow_value_flow.with_package.native.json"
    PACKAGE_ONLY
)

# Auto-discover and register all with_package golden tests.
# This replaces ~88 manual add_test() calls by scanning tests/golden/ for
# files matching the *.with_package.* naming convention.
ahfl_discover_package_golden_tests()

ahfl_add_output_test(
    ahflc.emit_native_json.expr_temporal
    "emit native-json"
    "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
    "${AHFL_TESTS_DIR}/golden/native/ok_expr_temporal.native.json"
)

ahfl_add_manifest_check_test(
    ahflc.check.import_alias
    "${AHFL_IMPORT_ALIAS_MANIFEST}"
    ok
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

ahfl_add_manifest_check_fail_test(
    ahflc.fail.duplicate_import_alias
    "${AHFL_IMPORT_ALIAS_MANIFEST}"
    duplicate
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
    ahflc.fail.workflow_input_schema_not_struct
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_input_schema_not_struct.ahfl"
    "workflow input type must resolve to a struct type"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_struct_field
    "${AHFL_TESTS_DIR}/golden/typecheck/duplicate_struct_field.ahfl"
    "duplicate struct field 'value'"
)

ahfl_add_check_fail_test(
    ahflc.fail.duplicate_enum_variant
    "${AHFL_TESTS_DIR}/golden/typecheck/duplicate_enum_variant.ahfl"
    "duplicate enum variant 'Pending'"
)

ahfl_add_check_fail_test(
    ahflc.fail.context_default_missing
    "${AHFL_TESTS_DIR}/golden/typecheck/context_default_missing.ahfl"
    "agent context field 'value' must declare a default value"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_context_default_schema_mismatch
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_context_default_schema_mismatch.ahfl"
    "exact schema mismatch in agent context default"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.container_variance_map_key_mismatch
    "${AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST}"
    map-key-mismatch
    "expected std::collections::Map<String, Int>, got std::collections::Map<String\\(2, 8\\), Int>"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.container_variance_optional_reverse_mismatch
    "${AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST}"
    optional-reverse-mismatch
    "expected std::option::Option<String\\(2, 8\\)>, got std::option::Option<String>"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.container_variance_list_reverse_mismatch
    "${AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST}"
    list-reverse-mismatch
    "expected std::collections::List<String\\(2, 8\\)>, got std::collections::List<String>"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.container_variance_set_reverse_mismatch
    "${AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST}"
    set-reverse-mismatch
    "expected std::collections::Set<String\\(2, 8\\)>, got std::collections::Set<String>"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.container_variance_map_value_reverse_mismatch
    "${AHFL_CONTAINER_VARIANCE_FAILURES_MANIFEST}"
    map-value-reverse-mismatch
    "expected std::collections::Map<String, String\\(2, 8\\)>, got std::collections::Map<String, String>"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_widening_int_float
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_widening_int_float.ahfl"
    "type mismatch in const initializer: expected Float, got Int"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_widening_int_decimal
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_widening_int_decimal.ahfl"
    "type mismatch in const initializer: expected Decimal\\(2\\), got Int"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_widening_decimal_scale
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_widening_decimal_scale.ahfl"
    "type mismatch in const initializer: expected Decimal\\(4\\), got Decimal\\(2\\)"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_widening_decimal_float
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_widening_decimal_float.ahfl"
    "type mismatch in const initializer: expected Float, got Decimal\\(2\\)"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_widening_decimal_scale_reverse
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_widening_decimal_scale_reverse.ahfl"
    "type mismatch in const initializer: expected Decimal\\(2\\), got Decimal\\(4\\)"
)

ahfl_add_check_fail_test(
    ahflc.fail.numeric_operator_int_float
    "${AHFL_TESTS_DIR}/golden/typecheck/numeric_operator_int_float.ahfl"
    "typecheck.INVALID_OPERATION"
)

ahfl_add_check_fail_test(
    ahflc.fail.readonly_input_assignment
    "${AHFL_TESTS_DIR}/golden/typecheck/readonly_input_assignment.ahfl"
    "assignment target must be rooted at writable 'ctx'"
)

ahfl_add_check_fail_test(
    ahflc.fail.const_expr_predicate_call
    "${AHFL_TESTS_DIR}/golden/typecheck/const_expr_predicate_call.ahfl"
    "const expression required in const initializer"
)

ahfl_add_check_fail_test(
    ahflc.fail.const_expr_path_reference
    "${AHFL_TESTS_DIR}/golden/typecheck/const_expr_path_reference.ahfl"
    "runtime path references are not compile-time constants"
)

ahfl_add_check_fail_test(
    ahflc.fail.if_condition_not_bool
    "${AHFL_TESTS_DIR}/golden/typecheck/if_condition_not_bool.ahfl"
    "if condition must have type Bool"
)

ahfl_add_check_fail_test(
    ahflc.fail.assert_condition_not_bool
    "${AHFL_TESTS_DIR}/golden/typecheck/assert_condition_not_bool.ahfl"
    "type mismatch in assert condition: expected Bool, got"
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
    ahflc.fail.workflow_duplicate_node
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_duplicate_node.ahfl"
    "duplicate workflow node 'run'"
)

ahfl_add_check_fail_test(
    ahflc.fail.temporal_unknown_workflow_node
    "${AHFL_TESTS_DIR}/golden/typecheck/temporal_unknown_workflow_node.ahfl"
    "unknown workflow node 'ghost'"
)

ahfl_add_check_fail_test(
    ahflc.fail.temporal_completed_in_agent_contract
    "${AHFL_TESTS_DIR}/golden/typecheck/temporal_completed_in_agent_contract.ahfl"
    "completed\\(\\.\\.\\.\\) is only valid in workflow safety/liveness formulas"
)

ahfl_add_check_fail_test(
    ahflc.fail.temporal_called_in_workflow
    "${AHFL_TESTS_DIR}/golden/typecheck/temporal_called_in_workflow.ahfl"
    "called\\(\\.\\.\\.\\) is only valid in agent contracts"
)

ahfl_add_check_fail_test(
    ahflc.fail.temporal_in_state_in_workflow
    "${AHFL_TESTS_DIR}/golden/typecheck/temporal_in_state_in_workflow.ahfl"
    "in_state\\(\\.\\.\\.\\) is only valid in agent contracts"
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
    ahflc.fail.agent_duplicate_state
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_duplicate_state.ahfl"
    "duplicate agent state 'Init'"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_initial_state_not_declared
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_initial_state_not_declared.ahfl"
    "initial state 'Ghost' is not declared in agent states"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_duplicate_final_state
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_duplicate_final_state.ahfl"
    "duplicate final state 'Done'"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_final_state_not_declared
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_final_state_not_declared.ahfl"
    "final state 'Ghost' is not declared in agent states"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_transition_source_not_declared
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_transition_source_not_declared.ahfl"
    "transition source state 'Ghost' is not declared in agent states"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_transition_target_not_declared
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_transition_target_not_declared.ahfl"
    "transition target state 'Ghost' is not declared in agent states"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_final_state_outgoing_transition
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_final_state_outgoing_transition.ahfl"
    "final state 'Done' must not have outgoing transitions"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_handler_state_not_declared
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_handler_state_not_declared.ahfl"
    "flow handler state 'Bogus' is not declared in the target agent"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_duplicate_handler
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_duplicate_handler.ahfl"
    "duplicate flow handler for state 'Init'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_missing_final_state_handler
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_missing_final_state_handler.ahfl"
    "missing final-state handler for 'Done'"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_final_handler_must_return
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_final_handler_must_return.ahfl"
    "final-state handler 'Done' must end with return on all control paths"
)

ahfl_add_check_fail_test(
    ahflc.fail.flow_non_final_handler_must_goto
    "${AHFL_TESTS_DIR}/golden/typecheck/flow_non_final_handler_must_goto.ahfl"
    "non-final-state handler 'Init' must end with goto on all control paths"
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
    "exact schema mismatch in workflow node input"
)

ahfl_add_check_fail_test(
    ahflc.fail.workflow_return_schema_mismatch
    "${AHFL_TESTS_DIR}/golden/typecheck/workflow_return_schema_mismatch.ahfl"
    "exact schema mismatch in workflow output"
)

ahfl_add_check_fail_test(
    ahflc.fail.agent_output_schema_mismatch
    "${AHFL_TESTS_DIR}/golden/typecheck/agent_output_schema_mismatch.ahfl"
    "exact schema mismatch in agent output"
)

# ---------------------------------------------------------------------------
# P5: SMV golden-lock baseline
# Aggregates all formal SMV golden outputs under a single CTest so any
# compiler change that perturbs SMV emission is caught immediately. On
# mismatch, the script prints a copy-pasteable `diff -u` command and the
# unified diff inline, which reviewers must see attached to PRs.
# ---------------------------------------------------------------------------

add_test(NAME p5_smv_golden_lock
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/p5_smv_golden_lock.py"
            "--ahflc=$<TARGET_FILE:ahflc>"
            "--tests-dir=${AHFL_TESTS_DIR}"
)
set_tests_properties(p5_smv_golden_lock PROPERTIES
    FAIL_REGULAR_EXPRESSION "golden mismatch|negative case did not trigger"
    LABELS "p5;golden_lock;smv;formal"
)

# Negative-path counterpart: validates that the diff-lock itself does fail
# (with the exact diagnostic strings) when expected vs. actual diverge.
add_test(NAME p5_smv_golden_lock.negative_diag
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/p5_smv_golden_lock.py"
            "--ahflc=$<TARGET_FILE:ahflc>"
            "--tests-dir=${AHFL_TESTS_DIR}"
            "--negative-only"
)
set_tests_properties(p5_smv_golden_lock.negative_diag PROPERTIES
    PASS_REGULAR_EXPRESSION "negative self-test passed.*golden mismatch.*diff cmd :.*diff -u"
    FAIL_REGULAR_EXPRESSION "negative case did not trigger|ERROR: negative"
    LABELS "p5;golden_lock;smv;formal;negative"
)

# ---------------------------------------------------------------------------
# stdlib unit-test matrix (corelib-support-workplan M0-3 / blocker B3).
# Positive fixtures cover every tracked stdlib_units/*_ut.ahfl module. The five
# option_neg_* fixtures are the negative-path counterpart.
# ---------------------------------------------------------------------------
ahfl_add_manifest_check_test(
    ahflc.check.stdlib_option_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-ut
)
set_tests_properties(ahflc.check.stdlib_option_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;option"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_result_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    result-ut
)
set_tests_properties(ahflc.check.stdlib_result_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;result"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_string_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    string-ut
)
set_tests_properties(ahflc.check.stdlib_string_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;string"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_list_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    list-ut
)
set_tests_properties(ahflc.check.stdlib_list_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;list"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_map_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    map-ut
)
set_tests_properties(ahflc.check.stdlib_map_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;map"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_set_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    set-ut
)
set_tests_properties(ahflc.check.stdlib_set_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;set"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_cmp_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    cmp-ut
)
set_tests_properties(ahflc.check.stdlib_cmp_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;cmp"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_time_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    time-ut
)
set_tests_properties(ahflc.check.stdlib_time_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;time"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_uuid_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    uuid-ut
)
set_tests_properties(ahflc.check.stdlib_uuid_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;uuid"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_json_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    json-ut
)
set_tests_properties(ahflc.check.stdlib_json_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;json"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_decimal_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    decimal-ut
)
set_tests_properties(ahflc.check.stdlib_decimal_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;decimal"
)

ahfl_add_manifest_check_test(
    ahflc.check.stdlib_prelude_ut
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    prelude-ut
)
set_tests_properties(ahflc.check.stdlib_prelude_ut PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
    LABELS "stdlib;unit;prelude"
)

ahfl_add_manifest_check_fail_test(
    ahflc.fail.stdlib_option_neg_map
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-neg-map
    "got Fn"
)
ahfl_add_manifest_check_fail_test(
    ahflc.fail.stdlib_option_neg_unwrap_or
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-neg-unwrap-or
    "got Bool"
)
ahfl_add_manifest_check_fail_test(
    ahflc.fail.stdlib_option_neg_not_option
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-neg-not-option
    "got Int"
)
ahfl_add_manifest_check_fail_test(
    ahflc.fail.stdlib_option_neg_arity
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-neg-arity
    "WRONG_ARITY"
)
ahfl_add_manifest_check_fail_test(
    ahflc.fail.stdlib_option_neg_unknown
    "${AHFL_STDLIB_UNITS_MANIFEST}"
    option-neg-unknown
    "UNKNOWN_CALLABLE"
)

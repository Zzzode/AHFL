ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-model
    TESTS
        ahflc.dump_package_graph.workspace_basic
        ahflc.check.discover_nested_package_uses_nearest_manifest
        ahflc.check.discover_nested_package_rejects_parent_target
        ahflc.check.manifest_sysroot_option_overrides_env
        ahflc.check.manifest_requires_canonical_filename
        ahflc.check.manifest_rejects_noncanonical_toml_filename
        ahflc.check.workspace_rejects_noncanonical_toml_filename
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-project-debug
    TESTS
        ahflc.dump_package_graph.manifest_basic
        ahflc.dump_lockfile.manifest_basic
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-semantics
    TESTS
        ahflc.check.workspace.check_ok_cross_file
        ahflc.check.workspace.fail_node_input
        ahflc.check.manifest_basic
        ahflc.check.workspace_basic
        ahflc.check.workspace_directory_module_export
        ahfl.check.project.ok_expression_type_isolated
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-ir
    TESTS
        ahflc.emit_ir.workflow_value_flow
        ahflc.emit_ir_json.workflow_value_flow
        ahfl.check.project.ok_expression_type_isolated
        ahfl.handoff.package_compat.escape_control_characters
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-backend
    TESTS
        ahflc.emit_summary.workflow_value_flow
        ahflc.emit_summary.manifest.workflow_value_flow
        ahflc.emit_smv.decreases.ok_decreases_length_self
)

ahfl_label_tests(
    LABELS ahfl-v0.3 v0.3-compat
    TESTS
        ahflc.check.search_root_removed
        ahflc.check.detached_import_rejected
        ahflc.check.detached_primitive_only
        ahflc.dump_project.removed
        ahflc.check.manifest_rejects_legacy_project_json
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
        ahflc.emit_native_json.rejects_legacy_package_json
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
        ahfl.runtime.enum_variant_e2e
        ahfl.runtime.if_let_e2e
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
        ahflc.run.llm_provider_runtime.smoke
        ahflc.run.llm_failure_matrix.smoke
        ahflc.run.llm_secret_manager.smoke
        ahflc.run.capability_bindings.smoke
        ahflc.run.input_schema.fail_missing_field
        ahflc.run.manifest.entry_workflow_default
        ahflc.run.default_manifest.entry_workflow_default
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
        ahfl.runtime.native_grpc_gate
        ahfl.runtime.transport_gate_smoke
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
        ahflc.passes.workflow_simplification_backend_effect
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
    LABELS ahfl-controlled-pilot
    TESTS
        ahfl.product.controlled_pilot_gate_contract
        ahfl.product.controlled_pilot_gate_smoke
        ahfl.runtime.execution_otel_all
        ahfl.runtime.workflow_recovery_all
        ahfl.reference_workflow.recovery_smoke
        ahflc.run.llm_provider_runtime.smoke
        ahfl.reference_workflow.production_matrix
        ahfl.product.controlled_pilot_gate_ready
)

ahfl_label_tests(
    LABELS ahfl-beta-gate
    TESTS
        ahfl.product.beta_gate_contract
        ahfl.product.beta_gate_smoke
        ahfl.product.runtime_evidence_smoke
        ahfl.product.formatter_evidence_smoke
        ahfl.product.stdlib_container_evidence_smoke
        ahfl.product.install_evidence_smoke
        ahfl.product.readme_capabilities_smoke
        ahfl.product.scope_freeze_smoke
        ahfl.product.beta_evidence_bundle_smoke
        ahfl.product.beta_evidence_bundle_ready
)

set_tests_properties(
    ahfl.product.beta_evidence_bundle_ready
    PROPERTIES
        DEPENDS
            "ahfl.product.beta_gate_contract;ahfl.product.beta_gate_smoke;ahfl.product.runtime_evidence_smoke;ahfl.product.formatter_evidence_smoke;ahfl.product.stdlib_container_evidence_smoke;ahfl.product.install_evidence_smoke;ahfl.product.readme_capabilities_smoke;ahfl.product.scope_freeze_smoke;ahfl.product.beta_evidence_bundle_smoke"
)

ahfl_label_tests(
    LABELS ahfl-production-confidence-contract
    TESTS
        ahfl.product.production_confidence_gate_contract
        ahfl.product.production_confidence_gate_smoke
        ahfl.product.production_confidence_ci_only_smoke
        ahfl.reference_workflow.long_soak_smoke
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
        ahflc.quality.smv_size_budget.workflow_simplification
        ahflc.quality.smv_size_budget.refund_audit
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-docs
    TESTS
        ahfl.docs.ir_sync_gate
        ahfl.docs.rfc_check
        ahfl.docs.rfc_check_smoke
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
        ahfl.architecture.boundaries
        ahfl.runtime.native_grpc_gate
        ahfl.runtime.transport_gate_smoke
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
        ahflc.quality.smv_size_budget.workflow_simplification
        ahflc.quality.smv_size_budget.refund_audit
        ahfl.mutation.config_report
)

set(AHFL_WORKFLOW_VALUE_FLOW_MANIFEST "${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/ahfl.toml")
set(AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE "${AHFL_TESTS_DIR}/integration/workflow_value_flow/ahfl.workspace.toml")
set(AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target workflow --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_AGENT_ENTRY_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target agent-entry --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_BAD_CAPABILITY_ARGS "--manifest ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST} --target bad-capability --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE_ARGS "--workspace ${AHFL_WORKFLOW_VALUE_FLOW_WORKSPACE} --package workflow-value-flow --target workflow --sysroot ${PROJECT_SOURCE_DIR}")
set(AHFL_CHECK_OK_WORKSPACE "${AHFL_TESTS_DIR}/integration/check_ok/ahfl.workspace.toml")
set(AHFL_CHECK_OK_WORKSPACE_ARGS --workspace "${AHFL_CHECK_OK_WORKSPACE}" --package check-ok-app --target workflow --sysroot "${PROJECT_SOURCE_DIR}")
set(AHFL_CHECK_FAIL_INPUT_WORKSPACE "${AHFL_TESTS_DIR}/integration/check_fail_input/ahfl.workspace.toml")
set(AHFL_STDLIB_API_SMOKE_MANIFEST "${AHFL_TESTS_DIR}/integration/stdlib_api_smoke/app/ahfl.toml")
set(AHFL_STDLIB_API_SMOKE_ARGS --manifest "${AHFL_STDLIB_API_SMOKE_MANIFEST}" --target lib --sysroot "${PROJECT_SOURCE_DIR}")
set(AHFL_PRELUDE_EXPLICIT_MANIFEST "${AHFL_TESTS_DIR}/integration/prelude_explicit/app/ahfl.toml")
set(AHFL_PRELUDE_EXPLICIT_ARGS --manifest "${AHFL_PRELUDE_EXPLICIT_MANIFEST}" --target lib --sysroot "${PROJECT_SOURCE_DIR}")
set(AHFL_DECREASES_LENGTH_SELF_MANIFEST "${AHFL_TESTS_DIR}/integration/package_golden/ok_decreases_length_self/ahfl.toml")

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

add_test(NAME ahfl.frontend.project.std_package_dependency_gates_imports
    COMMAND $<TARGET_FILE:ahfl_project_parse_tests>
            std-package-dependency-gates-imports
            "${CMAKE_BINARY_DIR}/std_package_dependency_gates_imports"
)

add_test(NAME ahfl.frontend.project.std_import_requires_explicit_module_root
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}"
            $<TARGET_FILE:ahfl_project_parse_tests>
            std-import-requires-explicit-module-root
            "${CMAKE_BINARY_DIR}/std_import_requires_explicit_module_root"
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

add_test(NAME ahfl.check.project.primitive_shadowing_forbidden
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            primitive-shadowing-forbidden
            "${CMAKE_BINARY_DIR}/primitive_shadowing_forbidden"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.primitive_facade_method_visibility
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            primitive-facade-method-visibility
            "${CMAKE_BINARY_DIR}/primitive_facade_method_visibility"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.inherent_method_visibility
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            inherent-method-visibility
            "${CMAKE_BINARY_DIR}/inherent_method_visibility"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.symbol_visibility_api_reachability
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            symbol-visibility-api-reachability
            "${CMAKE_BINARY_DIR}/symbol_visibility_api_reachability"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.symbol_visibility_unreachable_public
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            symbol-visibility-unreachable-public
            "${CMAKE_BINARY_DIR}/symbol_visibility_unreachable_public"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.symbol_visibility_duplicate_modifier
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            symbol-visibility-duplicate-modifier
            "${CMAKE_BINARY_DIR}/symbol_visibility_duplicate_modifier"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.check.project.symbol_visibility_handoff_export
    COMMAND $<TARGET_FILE:ahfl_project_check_tests>
            symbol-visibility-handoff-export
            "${CMAKE_BINARY_DIR}/symbol_visibility_handoff_export"
            "${PROJECT_SOURCE_DIR}"
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
            "${AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE}"
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

add_test(NAME ahflc.check.search_root_removed
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "-DAHFLC_ARGS=check\;--search-root\;${AHFL_TESTS_DIR}/integration/ok\;${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "-DEXPECTED_REGEX=--search-root has been removed"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.detached_import_rejected
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/.."
            ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/single_file_std_import/main.ahfl"
            "-DAHFLC_ARGS=check\;--sysroot\;${PROJECT_SOURCE_DIR}\;${AHFL_TESTS_DIR}/integration/single_file_std_import/main.ahfl"
            "-DEXPECTED_REGEX=E::detached_import"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.detached_primitive_only
    COMMAND $<TARGET_FILE:ahflc>
            check
            --sysroot "${PROJECT_SOURCE_DIR}"
            "${AHFL_TESTS_DIR}/golden/formatter/formatted_struct_2spaces.ahfl"
)
set_tests_properties(ahflc.check.detached_primitive_only PROPERTIES
    PASS_REGULAR_EXPRESSION "N::detached_source_unit"
)

add_test(NAME ahflc.dump_project.removed
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "-DAHFLC_ARGS=dump\;project\;${AHFL_TESTS_DIR}/integration/ok/app/main.ahfl"
            "-DEXPECTED_REGEX=unknown artifact 'project' for action 'dump'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
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

add_test(NAME ahflc.check.manifest_accepts_sysroot_manifest_input
    COMMAND $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}/std/ahfl.toml"
)
set_tests_properties(ahflc.check.manifest_accepts_sysroot_manifest_input PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.source_sysroot_manifest
    COMMAND $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/source_sysroot_cli/std/ahfl.toml"
            --sysroot "${AHFL_TESTS_DIR}/integration/source_sysroot_cli"
)
set_tests_properties(ahflc.check.source_sysroot_manifest PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 4 source\\(s\\)"
)

add_test(NAME ahflc.check.source_sysroot_manifest_accepts_sysroot_manifest_input
    COMMAND $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/source_sysroot_cli/std/ahfl.toml"
            --sysroot "${AHFL_TESTS_DIR}/integration/source_sysroot_cli/std/ahfl.toml"
)
set_tests_properties(ahflc.check.source_sysroot_manifest_accepts_sysroot_manifest_input PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 4 source\\(s\\)"
)

add_test(NAME ahflc.check.source_sysroot_positional_file
    COMMAND $<TARGET_FILE:ahflc> check
            "${AHFL_TESTS_DIR}/integration/source_sysroot_cli/std/json.ahfl"
            --sysroot "${AHFL_TESTS_DIR}/integration/source_sysroot_cli"
)
set_tests_properties(ahflc.check.source_sysroot_positional_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.dump_package_graph.source_sysroot_manifest
    COMMAND $<TARGET_FILE:ahflc> dump package-graph
            --manifest "${AHFL_TESTS_DIR}/integration/source_sysroot_cli/std/ahfl.toml"
            --sysroot "${AHFL_TESTS_DIR}/integration/source_sysroot_cli"
)
set_tests_properties(ahflc.dump_package_graph.source_sysroot_manifest PROPERTIES
    PASS_REGULAR_EXPRESSION "\"packages\":\\[\\{\"id\":0,\"name\":\"std\".*\"source\":\"sysroot\".*\"dependencies\":\\[\\].*\"module_roots\":\\[\\{\"prefix\":\"std\",\"package\":0"
)

add_test(NAME ahfl.release_evidence_archive.smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/generate-release-evidence-archive.py"
            --ahflc $<TARGET_FILE:ahflc>
            --repo-root "${PROJECT_SOURCE_DIR}"
            --out-dir "${CMAKE_CURRENT_BINARY_DIR}/release-evidence-archive"
)
set_tests_properties(ahfl.release_evidence_archive.smoke PROPERTIES
    LABELS "release-evidence-archive;rfc0005;rfc0006;rfc0007;rfc0009;rfc0010;rfc0011"
)

add_test(NAME ahflc.check.manifest_rejects_std_directory_sysroot_input
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}/std"
            "-DEXPECTED_REGEX=E::toolchain_sysroot_invalid"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.dump_package_graph.reports_sysroot_mismatch_for_corelib_manifest
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${PROJECT_SOURCE_DIR}/std/ahfl.toml"
            "-DAHFLC_ARGS=dump\;package-graph\;--manifest\;${PROJECT_SOURCE_DIR}/std/ahfl.toml\;--sysroot\;${AHFL_TESTS_DIR}/integration/alternate_sysroot"
            "-DEXPECTED_REGEX=E::toolchain_sysroot_mismatch"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
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

add_test(NAME ahflc.check.manifest_sysroot_option_overrides_env
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/.."
            ${CMAKE_COMMAND} -E env
            --unset=AHFL_STDLIB_SEARCH_ROOT
            "AHFL_SYSROOT=${CMAKE_BINARY_DIR}/missing-sysroot"
            $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.manifest_sysroot_option_overrides_env PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_ignores_stdlib_search_root_env
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/.."
            ${CMAKE_COMMAND} -E env
            --unset=AHFL_SYSROOT
            "AHFL_STDLIB_SEARCH_ROOT=${PROJECT_SOURCE_DIR}"
            $<TARGET_FILE:ahflc> check
            --manifest "${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.toml"
            --target workflow
)
set_tests_properties(ahflc.check.manifest_ignores_stdlib_search_root_env PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.manifest_requires_canonical_filename
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/check_ok/app/main.ahfl\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=--manifest expects ahfl\\.toml"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.manifest_rejects_noncanonical_toml_filename
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/package.toml"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/package.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
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

add_test(NAME ahflc.check.manifest_rejects_workspace_dependency_source
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/packages/refund-audit/ahfl.toml"
            "-DAHFLC_ARGS=check\;--manifest\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/packages/refund-audit/ahfl.toml\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=workspace dependency 'audit-core' requires workspace manifest context"
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

add_test(NAME ahflc.check.discover_nested_package_uses_nearest_manifest
    COMMAND $<TARGET_FILE:ahflc> check
            "${AHFL_TESTS_DIR}/integration/package_graph_nested/nested/src/main.ahfl"
            --target child
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.discover_nested_package_uses_nearest_manifest PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked [0-9]+ source\\(s\\)"
)

add_test(NAME ahflc.check.discover_nested_package_rejects_parent_target
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_nested/nested/src/main.ahfl"
            "-DAHFLC_ARGS=check\;${AHFL_TESTS_DIR}/integration/package_graph_nested/nested/src/main.ahfl\;--target\;parent\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package 'child-package' does not contain target 'parent'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
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

add_test(NAME ahflc.check.workspace_lockfile_drift_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DSOURCE_WORKSPACE=${AHFL_TESTS_DIR}/integration/package_graph_workspace"
            "-DSYSROOT_DIR=${PROJECT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/package_graph_workspace_lock_drift"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunWorkspaceLockfileDriftTest.cmake"
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
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
)

add_test(NAME ahflc.check.workspace_requires_target_for_multi_target_package
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml\;--package\;refund-audit\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package 'refund-audit' contains 4 targets; pass --target <name>"
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

add_test(NAME ahflc.check.workspace_rejects_noncanonical_toml_filename
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/workspace.toml\;--package\;refund-audit\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
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
    PASS_REGULAR_EXPRESSION "ok: checked 3 source\\(s\\)"
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

add_test(NAME ahflc.check.workspace_directory_module_export
    COMMAND $<TARGET_FILE:ahflc> check
            --workspace "${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            --package refund-audit
            --target directory-export
            --sysroot "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(ahflc.check.workspace_directory_module_export PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked [0-9]+ source\\(s\\)"
)

add_test(NAME ahflc.check.workspace_member_import_requires_dependency
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/integration/package_graph_workspace/ahfl.workspace.toml\;--package\;no-deps-app\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_REGEX=package prefix 'no_deps_app' does not depend on package prefix 'audit_core'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.check.workspace.check_ok_cross_file
    COMMAND $<TARGET_FILE:ahflc> check
            ${AHFL_CHECK_OK_WORKSPACE_ARGS}
)
set_tests_properties(ahflc.check.workspace.check_ok_cross_file PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
)

add_test(NAME ahflc.check.stdlib_api_smoke
    COMMAND $<TARGET_FILE:ahflc> check
            ${AHFL_STDLIB_API_SMOKE_ARGS}
)
set_tests_properties(ahflc.check.stdlib_api_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
)

add_test(NAME ahflc.check.explicit_prelude_import
    COMMAND $<TARGET_FILE:ahflc> check
            ${AHFL_PRELUDE_EXPLICIT_ARGS}
)
set_tests_properties(ahflc.check.explicit_prelude_import PROPERTIES
    PASS_REGULAR_EXPRESSION "ok: checked"
)

add_test(NAME ahflc.check.project_implicit_prelude_rejected
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/prelude_explicit/implicit_prelude.ahfl"
            "-DAHFLC_ARGS=check\;${AHFL_TESTS_DIR}/integration/prelude_explicit/implicit_prelude.ahfl"
            "-DEXPECTED_REGEX=unknown callable 'some'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.package_requires_workspace
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DAHFLC_ARGS=emit\;native-json\;--package\;workflow-value-flow\;${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
            "-DEXPECTED_REGEX=--package is only supported with --workspace <ahfl\\.workspace\\.toml>"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_test(NAME ahflc.emit_native_json.rejects_legacy_package_json
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.package.json"
            "-DAHFLC_ARGS=emit\;native-json\;--package\;${AHFL_TESTS_DIR}/integration/package_graph_manifest/ahfl.package.json\;${AHFL_TESTS_DIR}/integration/workflow_value_flow/app/main.ahfl"
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


add_test(NAME ahflc.emit_summary.manifest.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit summary ${AHFL_WORKFLOW_VALUE_FLOW_MANIFEST_ARGS}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/summary/project_workflow_value_flow.summary"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.run.manifest.entry_workflow_default
    COMMAND $<TARGET_FILE:ahflc> run
            --manifest "${AHFL_TESTS_DIR}/integration/package_golden/ok_workflow_value_flow/ahfl.toml"
            --sysroot "${PROJECT_SOURCE_DIR}"
            --input "{\"_type\":\"ir::workflow_value_flow::Request\",\"value\":\"package-entry\"}"
            --llm-config "${AHFL_TESTS_DIR}/golden/runtime/llm_config_test_key.json"
)
set_tests_properties(ahflc.run.manifest.entry_workflow_default PROPERTIES
    PASS_REGULAR_EXPRESSION "ir::workflow_value_flow::ValueFlowWorkflow  completed"
)

add_test(NAME ahflc.run.default_manifest.entry_workflow_default
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DWORKING_DIRECTORY=${AHFL_TESTS_DIR}/integration/package_golden/ok_workflow_value_flow"
            "-DAHFLC_ARGS=run\;--sysroot\;${PROJECT_SOURCE_DIR}\;--input\;{\"_type\":\"ir::workflow_value_flow::Request\",\"value\":\"package-entry\"}\;--llm-config\;${AHFL_TESTS_DIR}/golden/runtime/llm_config_test_key.json"
            "-DEXPECTED_REGEX=ir::workflow_value_flow::ValueFlowWorkflow  completed"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunCommandRegex.cmake"
)

add_test(NAME ahflc.emit_smv.decreases.ok_decreases_length_self
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit smv --manifest ${AHFL_DECREASES_LENGTH_SELF_MANIFEST} --target workflow --sysroot ${PROJECT_SOURCE_DIR}"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/golden/formal/ok_decreases_length_self.smv"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
)

add_test(NAME ahflc.check.workspace.fail_node_input
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_CHECK_FAIL_INPUT_WORKSPACE}"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_CHECK_FAIL_INPUT_WORKSPACE}\;--package\;check-fail-input-app\;--target\;workflow\;--sysroot\;${PROJECT_SOURCE_DIR}"
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

add_test(NAME ahfl.runtime.execution_event_all
    COMMAND $<TARGET_FILE:ahfl_execution_event_tests>
)

add_test(NAME ahfl.runtime.execution_report_all
    COMMAND $<TARGET_FILE:ahfl_execution_report_tests>
)

add_test(NAME ahfl.runtime.execution_metadata_all
    COMMAND $<TARGET_FILE:ahfl_execution_metadata_tests>
)

add_test(NAME ahfl.runtime.execution_renderer_all
    COMMAND $<TARGET_FILE:ahfl_execution_renderer_tests>
)

add_test(NAME ahfl.runtime.execution_projection_all
    COMMAND $<TARGET_FILE:ahfl_execution_projection_tests>
)

add_test(NAME ahfl.runtime.execution_otel_all
    COMMAND $<TARGET_FILE:ahfl_execution_otel_tests>
)

add_test(NAME ahfl.runtime.workflow_recovery_all
    COMMAND $<TARGET_FILE:ahfl_workflow_recovery_tests>
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

add_test(NAME ahfl.runtime.enum_variant_e2e
    COMMAND $<TARGET_FILE:ahfl_enum_variant_e2e_tests>
            "${AHFL_TESTS_DIR}/golden/runtime/enum_variant_e2e.ahfl"
)

add_test(NAME ahfl.runtime.if_let_e2e
    COMMAND $<TARGET_FILE:ahfl_if_let_e2e_tests>
            "${AHFL_TESTS_DIR}/golden/runtime/if_let_e2e.ahfl"
)

add_test(NAME ahfl.llm_provider.all
    COMMAND $<TARGET_FILE:ahfl_runtime_provider_llm_tests>
)

add_test(NAME ahfl.reference_workflow.recovery_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/reference_workflow_recovery_smoke.py"
            $<TARGET_FILE:ahfl_reference_workflow_recovery_worker>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/reference-workflow-recovery"
            "${PROJECT_SOURCE_DIR}/build/release-evidence/beta/reference-workflow-recovery.json"
)

add_test(NAME ahfl.reference_workflow.production_matrix
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/reference_workflow_production_matrix.py"
            $<TARGET_FILE:ahfl_reference_workflow_recovery_worker>
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime/reference-workflow-production-matrix"
            "12"
            "30"
            "${PROJECT_SOURCE_DIR}/build/release-evidence/pilot/reference-workflow-production-matrix.json"
)

add_test(NAME ahfl.product.controlled_pilot_gate_ready
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/check-controlled-pilot-gate.py"
            --root "${PROJECT_SOURCE_DIR}"
            --require-ready
)
set_tests_properties(
    ahfl.reference_workflow.production_matrix
    PROPERTIES
        DEPENDS "ahfl.runtime.execution_otel_all;ahfl.runtime.workflow_recovery_all;ahfl.reference_workflow.recovery_smoke;ahflc.run.llm_provider_runtime.smoke"
)
set_tests_properties(
    ahfl.product.controlled_pilot_gate_ready
    PROPERTIES
        DEPENDS "ahfl.reference_workflow.production_matrix"
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

add_test(NAME ahfl.semantics.pattern_usefulness_all
    COMMAND $<TARGET_FILE:ahfl_semantics_pattern_usefulness_tests>
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

add_test(NAME ahfl.semantics.try_operator_all
    COMMAND $<TARGET_FILE:ahfl_semantics_try_operator_tests>
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

# RFC 0001 enum struct variant syntax coverage: parse -> AST -> ast_printer ->
# formatter roundtrip. Semantic, Typed HIR, IR, and runtime coverage lives in
# the dedicated semantics/runtime suites.
add_test(NAME ahfl.frontend.enum_struct_variant_all
    COMMAND $<TARGET_FILE:ahfl_enum_struct_variant_tests>
)

# RFC 0011 if-let pattern syntax coverage: parse -> AST -> ast_printer ->
# formatter roundtrip.
add_test(NAME ahfl.frontend.if_let_syntax_all
    COMMAND $<TARGET_FILE:ahfl_if_let_syntax_tests>
)

add_test(NAME ahfl.support.thread_pool_all
    COMMAND $<TARGET_FILE:ahfl_thread_pool_tests>
)

add_test(NAME ahfl.support.sha256_all
    COMMAND $<TARGET_FILE:ahfl_sha256_tests>
)

add_test(NAME ahfl.support.atomic_file_all
    COMMAND $<TARGET_FILE:ahfl_atomic_file_tests>
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

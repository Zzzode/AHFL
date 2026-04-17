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

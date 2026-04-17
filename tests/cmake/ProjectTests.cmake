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

add_test(NAME ahfl.handoff.package.file_expr_temporal
    COMMAND $<TARGET_FILE:ahfl_handoff_package_tests>
            file-expr-temporal
            "${AHFL_TESTS_DIR}/ir/ok_expr_temporal.ahfl"
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

add_test(NAME ahflc.emit_native_json.workspace.workflow_value_flow
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DAHFLC_ARGS=emit-native-json --workspace ${AHFL_TESTS_DIR}/project/handoff.workspace.json --project-name workflow-value-flow"
            "-DEXPECTED_FILE=${AHFL_TESTS_DIR}/native/project_workflow_value_flow.native.json"
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

add_test(NAME ahflc.check.workspace.fail_duplicate_project_name
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/project/workspace_duplicate/ahfl.workspace.json"
            "-DAHFLC_ARGS=check\;--workspace\;${AHFL_TESTS_DIR}/project/workspace_duplicate/ahfl.workspace.json\;--project-name\;dup-project"
            "-DEXPECTED_REGEX=workspace contains duplicate project name 'dup-project'"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
)

add_executable(ahfl_project_parse_tests
    project/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    project/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_semantics
)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    project/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_ir
)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_handoff_package_tests
    handoff/package_model.cpp
)
target_link_libraries(ahfl_handoff_package_tests
    PRIVATE
        ahfl_handoff
)
ahfl_apply_project_warnings(ahfl_handoff_package_tests)

add_executable(ahfl_handoff_package_compat_tests
    handoff/package_compat.cpp
)
target_link_libraries(ahfl_handoff_package_compat_tests
    PRIVATE
        ahfl_backend_native_json
)
ahfl_apply_project_warnings(ahfl_handoff_package_compat_tests)

add_executable(ahfl_dry_run_tests
    dry_run/runner.cpp
)
target_link_libraries(ahfl_dry_run_tests
    PRIVATE
        ahfl_dry_run
)
ahfl_apply_project_warnings(ahfl_dry_run_tests)

add_executable(ahfl_runtime_session_tests
    runtime_session/session.cpp
)
target_link_libraries(ahfl_runtime_session_tests
    PRIVATE
        ahfl_runtime_session
)
ahfl_apply_project_warnings(ahfl_runtime_session_tests)

add_executable(ahfl_execution_journal_tests
    execution_journal/journal.cpp
)
target_link_libraries(ahfl_execution_journal_tests
    PRIVATE
        ahfl_execution_journal
)
ahfl_apply_project_warnings(ahfl_execution_journal_tests)

add_executable(ahfl_replay_view_tests
    replay_view/replay.cpp
)
target_link_libraries(ahfl_replay_view_tests
    PRIVATE
        ahfl_replay_view
        ahfl_ir
)
ahfl_apply_project_warnings(ahfl_replay_view_tests)

add_executable(ahfl_audit_report_tests
    audit_report/report.cpp
)
target_link_libraries(ahfl_audit_report_tests
    PRIVATE
        ahfl_audit_report
)
ahfl_apply_project_warnings(ahfl_audit_report_tests)

add_executable(ahfl_scheduler_snapshot_tests
    scheduler_snapshot/snapshot.cpp
)
target_link_libraries(ahfl_scheduler_snapshot_tests
    PRIVATE
        ahfl_scheduler_snapshot
)
ahfl_apply_project_warnings(ahfl_scheduler_snapshot_tests)

add_executable(ahfl_checkpoint_record_tests
    checkpoint_record/record.cpp
)
target_link_libraries(ahfl_checkpoint_record_tests
    PRIVATE
        ahfl_checkpoint_record
)
ahfl_apply_project_warnings(ahfl_checkpoint_record_tests)

add_executable(ahfl_persistence_descriptor_tests
    persistence_descriptor/descriptor.cpp
)
target_link_libraries(ahfl_persistence_descriptor_tests
    PRIVATE
        ahfl_persistence_descriptor
)
ahfl_apply_project_warnings(ahfl_persistence_descriptor_tests)

add_executable(ahfl_persistence_export_tests
    persistence_export/manifest.cpp
)
target_link_libraries(ahfl_persistence_export_tests
    PRIVATE
        ahfl_persistence_export
)
ahfl_apply_project_warnings(ahfl_persistence_export_tests)

add_executable(ahfl_store_import_tests
    store_import/descriptor.cpp
)
target_link_libraries(ahfl_store_import_tests
    PRIVATE
        ahfl_store_import
        ahfl_persistence_export
)
ahfl_apply_project_warnings(ahfl_store_import_tests)

add_executable(ahfl_durable_store_import_tests
    durable_store_import/request.cpp
)
target_link_libraries(ahfl_durable_store_import_tests
    PRIVATE
        ahfl_durable_store_import
        ahfl_store_import
)
ahfl_apply_project_warnings(ahfl_durable_store_import_tests)

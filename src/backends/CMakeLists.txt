add_library(ahfl_backend_smv STATIC
    smv.cpp
)

target_link_libraries(ahfl_backend_smv
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_smv)

add_library(ahfl_backend_summary STATIC
    summary.cpp
)

target_link_libraries(ahfl_backend_summary
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_summary)

add_library(ahfl_backend_native_json STATIC
    native_json.cpp
)

target_link_libraries(ahfl_backend_native_json
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_native_json)

add_library(ahfl_backend_execution_plan STATIC
    execution_plan.cpp
)

target_link_libraries(ahfl_backend_execution_plan
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_execution_plan)

add_library(ahfl_backend_execution_journal STATIC
    execution_journal.cpp
)

target_link_libraries(ahfl_backend_execution_journal
    PUBLIC
        ahfl_execution_journal
)

ahfl_apply_project_warnings(ahfl_backend_execution_journal)

add_library(ahfl_backend_replay_view STATIC
    replay_view.cpp
)

target_link_libraries(ahfl_backend_replay_view
    PUBLIC
        ahfl_replay_view
)

ahfl_apply_project_warnings(ahfl_backend_replay_view)

add_library(ahfl_backend_audit_report STATIC
    audit_report.cpp
)

target_link_libraries(ahfl_backend_audit_report
    PUBLIC
        ahfl_audit_report
)

ahfl_apply_project_warnings(ahfl_backend_audit_report)

add_library(ahfl_backend_scheduler_snapshot STATIC
    scheduler_snapshot.cpp
)

target_link_libraries(ahfl_backend_scheduler_snapshot
    PUBLIC
        ahfl_scheduler_snapshot
)

ahfl_apply_project_warnings(ahfl_backend_scheduler_snapshot)

add_library(ahfl_backend_checkpoint_record STATIC
    checkpoint_record.cpp
)

target_link_libraries(ahfl_backend_checkpoint_record
    PUBLIC
        ahfl_checkpoint_record
)

ahfl_apply_project_warnings(ahfl_backend_checkpoint_record)

add_library(ahfl_backend_checkpoint_review STATIC
    checkpoint_review.cpp
)

target_link_libraries(ahfl_backend_checkpoint_review
    PUBLIC
        ahfl_checkpoint_record
)

ahfl_apply_project_warnings(ahfl_backend_checkpoint_review)

add_library(ahfl_backend_scheduler_review STATIC
    scheduler_review.cpp
)

target_link_libraries(ahfl_backend_scheduler_review
    PUBLIC
        ahfl_scheduler_snapshot
)

ahfl_apply_project_warnings(ahfl_backend_scheduler_review)

add_library(ahfl_backend_dry_run_trace STATIC
    dry_run_trace.cpp
)

target_link_libraries(ahfl_backend_dry_run_trace
    PUBLIC
        ahfl_dry_run
)

ahfl_apply_project_warnings(ahfl_backend_dry_run_trace)

add_library(ahfl_backend_runtime_session STATIC
    runtime_session.cpp
)

target_link_libraries(ahfl_backend_runtime_session
    PUBLIC
        ahfl_runtime_session
)

ahfl_apply_project_warnings(ahfl_backend_runtime_session)

add_library(ahfl_backend_package_review STATIC
    package_review.cpp
)

target_link_libraries(ahfl_backend_package_review
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_package_review)

add_library(ahfl_backend STATIC
    driver.cpp
)

target_link_libraries(ahfl_backend
    PUBLIC
        ahfl_ir
        ahfl_backend_audit_report
        ahfl_backend_scheduler_review
        ahfl_backend_dry_run_trace
        ahfl_backend_execution_plan
        ahfl_backend_execution_journal
        ahfl_backend_replay_view
        ahfl_backend_scheduler_snapshot
        ahfl_backend_checkpoint_record
        ahfl_backend_checkpoint_review
        ahfl_backend_native_json
        ahfl_backend_package_review
        ahfl_backend_runtime_session
        ahfl_backend_summary
        ahfl_backend_smv
)

ahfl_apply_project_warnings(ahfl_backend)

add_library(ahfl_audit_report STATIC
    report.cpp
)

target_link_libraries(ahfl_audit_report
    PUBLIC
        ahfl_replay_view
)

ahfl_apply_project_warnings(ahfl_audit_report)

add_library(ahfl_pipeline_execution STATIC
    dry_run/mock.cpp
    dry_run/runner.cpp
    runtime_session/session.cpp
    execution_journal/journal.cpp
    replay_view/replay.cpp
)

target_link_libraries(ahfl_pipeline_execution
    PUBLIC
        ahfl_compiler_handoff
    PRIVATE
        ahfl_base_json
)
target_include_directories(ahfl_pipeline_execution PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_execution)

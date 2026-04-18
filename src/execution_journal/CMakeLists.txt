add_library(ahfl_execution_journal STATIC
    journal.cpp
)

target_link_libraries(ahfl_execution_journal
    PUBLIC
        ahfl_runtime_session
)

ahfl_apply_project_warnings(ahfl_execution_journal)

add_library(ahfl_replay_view STATIC
    replay.cpp
)

target_link_libraries(ahfl_replay_view
    PUBLIC
        ahfl_execution_journal
)

ahfl_apply_project_warnings(ahfl_replay_view)

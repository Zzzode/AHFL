add_library(ahfl_scheduler_snapshot STATIC
    snapshot.cpp
    review.cpp
)

target_link_libraries(ahfl_scheduler_snapshot
    PUBLIC
        ahfl_replay_view
)

ahfl_apply_project_warnings(ahfl_scheduler_snapshot)

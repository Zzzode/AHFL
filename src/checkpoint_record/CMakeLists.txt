add_library(ahfl_checkpoint_record STATIC
    record.cpp
    review.cpp
)

target_link_libraries(ahfl_checkpoint_record
    PUBLIC
        ahfl_scheduler_snapshot
)

ahfl_apply_project_warnings(ahfl_checkpoint_record)

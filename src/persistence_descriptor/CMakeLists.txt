add_library(ahfl_persistence_descriptor STATIC
    descriptor.cpp
    review.cpp
)

target_link_libraries(ahfl_persistence_descriptor
    PUBLIC
        ahfl_checkpoint_record
)

ahfl_apply_project_warnings(ahfl_persistence_descriptor)

add_library(ahfl_persistence_export STATIC
    manifest.cpp
    review.cpp
)

target_link_libraries(ahfl_persistence_export
    PUBLIC
        ahfl_persistence_descriptor
)

ahfl_apply_project_warnings(ahfl_persistence_export)

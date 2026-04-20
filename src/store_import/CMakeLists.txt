add_library(ahfl_store_import STATIC
    descriptor.cpp
    review.cpp
)

target_link_libraries(ahfl_store_import
    PUBLIC
        ahfl_persistence_export
)

ahfl_apply_project_warnings(ahfl_store_import)

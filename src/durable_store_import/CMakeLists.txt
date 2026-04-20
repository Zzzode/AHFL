add_library(ahfl_durable_store_import STATIC
    request.cpp
    review.cpp
)

target_link_libraries(ahfl_durable_store_import
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_durable_store_import)

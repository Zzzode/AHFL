add_library(ahfl_base_query STATIC
    query_engine.cpp
)

target_link_libraries(ahfl_base_query
    PUBLIC
        ahfl_base_public
)

ahfl_apply_project_warnings(ahfl_base_query)

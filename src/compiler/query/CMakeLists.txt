add_library(ahfl_compiler_query STATIC
    frontend_queries.cpp
)

target_link_libraries(ahfl_compiler_query
    PUBLIC
        ahfl_base_query
        ahfl_compiler_syntax
    PRIVATE
        # serialize_diagnostic_report_json: the canonical diagnostic projection
        # the parse snapshot compares on.
        ahfl_base_support
)

target_include_directories(ahfl_compiler_query PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_query)

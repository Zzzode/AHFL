add_library(ahfl_compiler_query STATIC
    frontend_queries.cpp
)

target_link_libraries(ahfl_compiler_query
    PUBLIC
        ahfl_base_query
        ahfl_compiler_syntax
        # The P3 semantic stages (resolve / typecheck) and their result types
        # (ResolveResult, TypeCheckResult, TypedProgram) are public: a query
        # consumer borrows the cached results, so the headers it must include are
        # part of this library's interface.
        ahfl_compiler_semantics
    PRIVATE
        # serialize_diagnostic_report_json: the canonical diagnostic projection
        # every stage snapshot compares on. serialize_typed_program_json: the
        # single canonical TypedProgram projection used as the typecheck memo
        # identity (no second typed-tree spelling).
        ahfl_base_support
)

target_include_directories(ahfl_compiler_query PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_query)

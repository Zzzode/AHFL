add_library(ahfl_compiler_semantics STATIC
    const_sema.cpp
    condition_facts.cpp
    effects.cpp
    expression_sema.cpp
    flow_facts.cpp
    name_suggestions.cpp
    monomorphization.cpp
    resolver.cpp
    type_resolver.cpp
    type_relations.cpp
    type_expectation.cpp
    typed_hir.cpp
    typed_hir_serialization.cpp
    types.cpp
    typecheck.cpp
    typecheck_decls.cpp
    typecheck_expr.cpp
    type_environment.cpp
    validate.cpp
)

target_link_libraries(ahfl_compiler_semantics
    PUBLIC
        ahfl_compiler_syntax
)

target_include_directories(ahfl_compiler_semantics PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_semantics)

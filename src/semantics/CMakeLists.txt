add_library(ahfl_semantics STATIC
    resolver.cpp
    typecheck.cpp
    validate.cpp
)

target_link_libraries(ahfl_semantics
    PUBLIC
        ahfl_syntax
)

ahfl_apply_project_warnings(ahfl_semantics)

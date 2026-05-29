add_library(ahfl_compiler_semantics STATIC
    resolver.cpp
    typecheck.cpp
    type_environment.cpp
    validate.cpp
)

target_link_libraries(ahfl_compiler_semantics
    PUBLIC
        ahfl_compiler_syntax
)

target_include_directories(ahfl_compiler_semantics PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_semantics)

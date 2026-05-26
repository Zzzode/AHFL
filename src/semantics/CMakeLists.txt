add_library(ahfl_semantics STATIC
    resolver.cpp
    typecheck.cpp
    type_environment.cpp
    validate.cpp
)

target_link_libraries(ahfl_semantics
    PUBLIC
        ahfl_syntax
)

target_include_directories(ahfl_semantics PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_semantics)

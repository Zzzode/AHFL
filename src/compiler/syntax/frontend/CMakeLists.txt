add_library(ahfl_compiler_syntax STATIC
    ast.cpp
    ast_printer.cpp
    desugar.cpp
    frontend.cpp
    project.cpp
)

target_link_libraries(ahfl_compiler_syntax
    PUBLIC
        ahfl_compiler_syntax_parser
        ahfl_base_public
    PRIVATE
        ahfl_base_json
)

target_include_directories(ahfl_compiler_syntax PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(ahfl_compiler_syntax PRIVATE AHFL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

ahfl_apply_project_warnings(ahfl_compiler_syntax)

add_library(ahfl_compiler_syntax_recovery STATIC
    error_recovery.cpp
)

target_link_libraries(ahfl_compiler_syntax_recovery
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_compiler_syntax_recovery PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_syntax_recovery)

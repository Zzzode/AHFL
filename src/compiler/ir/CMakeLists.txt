add_library(ahfl_compiler_ir STATIC
    analysis.cpp
    identity.cpp
    ir_lower.cpp
    ir_print.cpp
    ir_json.cpp
    program_view.cpp
    typed_hir_lower.cpp
    visitor.cpp
)

target_link_libraries(ahfl_compiler_ir
    PUBLIC
        ahfl_compiler_semantics
)

target_include_directories(ahfl_compiler_ir PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_ir)

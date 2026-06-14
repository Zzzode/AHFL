add_library(ahfl_compiler_ir_opt STATIC
    opt_ir.cpp
    opt_json.cpp
    opt_lower.cpp
    opt_passes.cpp
    opt_print.cpp
    opt_verify.cpp
)

target_link_libraries(ahfl_compiler_ir_opt
    PUBLIC
        ahfl_compiler_ir
)

target_include_directories(ahfl_compiler_ir_opt PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_ir_opt)

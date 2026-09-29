add_library(ahfl_runtime_evaluator STATIC
    eval_context.cpp
    pattern_match.cpp
    evaluator.cpp
    executor.cpp
    builtins.cpp
    runtime_fn_table.cpp
)

# The tree-walking evaluator (RFC 0026 P8 retirement target) consumes the host
# wire-value layer, which is a peer target now that the survivors live in
# src/runtime/value/. ahfl_base_json is no longer needed here: the JSON edges
# (value_json / scalar_spelling) moved with the survivor target.
target_link_libraries(ahfl_runtime_evaluator
    PUBLIC
        ahfl_runtime_value
        ahfl_compiler_ir
)

target_include_directories(ahfl_runtime_evaluator PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_evaluator)

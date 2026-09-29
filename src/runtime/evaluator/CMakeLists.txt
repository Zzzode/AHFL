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
# src/runtime/value/. ahfl_base_json reaches it transitively and PUBLICly
# through ahfl_runtime_value. ahfl_base_support is a PRIVATE edge of that
# target (its include dir is build-tree-only), and several evaluator TUs
# include base/support headers directly, so it is linked PRIVATE here as well.
target_link_libraries(ahfl_runtime_evaluator
    PUBLIC
        ahfl_runtime_value
        ahfl_compiler_ir
    PRIVATE
        ahfl_base_support
)

target_include_directories(ahfl_runtime_evaluator PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_evaluator)

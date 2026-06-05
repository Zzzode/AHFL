add_library(ahfl_runtime_evaluator STATIC
    value.cpp
    value_json.cpp
    eval_context.cpp
    evaluator.cpp
    executor.cpp
)

target_link_libraries(ahfl_runtime_evaluator
    PUBLIC
        ahfl_compiler_ir
    PRIVATE
        ahfl_base_json
)

target_include_directories(ahfl_runtime_evaluator PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_evaluator)

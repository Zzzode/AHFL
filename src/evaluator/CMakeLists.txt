add_library(ahfl_evaluator STATIC
    value.cpp
    eval_context.cpp
    evaluator.cpp
    executor.cpp
)

target_link_libraries(ahfl_evaluator
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_evaluator)

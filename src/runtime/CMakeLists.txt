add_library(ahfl_runtime STATIC
    agent_runtime.cpp
    workflow_runtime.cpp
    capability_bridge.cpp
    capability_eval.cpp
)

target_link_libraries(ahfl_runtime
    PUBLIC
        ahfl_evaluator
)

ahfl_apply_project_warnings(ahfl_runtime)

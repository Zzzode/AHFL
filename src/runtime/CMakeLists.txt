add_library(ahfl_runtime STATIC
    agent_runtime.cpp
    workflow_runtime.cpp
    capability_bridge.cpp
    capability_eval.cpp
    connection_pool.cpp
    http_transport.cpp
    grpc_transport.cpp
    parallel_scheduler.cpp
    data_pipeline.cpp
    sandbox.cpp
    distributed.cpp
)

target_link_libraries(ahfl_runtime
    PUBLIC
        ahfl_evaluator
    PRIVATE
        ahfl_support_lib
        ahfl_json
)

ahfl_apply_project_warnings(ahfl_runtime)

add_library(ahfl_runtime_engine STATIC
    agent_runtime.cpp
    workflow_runtime.cpp
    capability_bridge.cpp
    capability_eval.cpp
    capability_transport_adapter.cpp
    connection_pool.cpp
    http_transport.cpp
    grpc_transport.cpp
    parallel_scheduler.cpp
    data_pipeline.cpp
    sandbox.cpp
    distributed.cpp
    wire_capability.cpp
    wire_value.cpp
    wire_transport_adapter.cpp
    response_schema_validator.cpp
)

target_link_libraries(ahfl_runtime_engine
    PUBLIC
        ahfl_runtime_evaluator
        ahfl_runtime_provider_secret
        ahfl_compiler_ir
    PRIVATE
        ahfl_base_support
        ahfl_base_json
)

target_include_directories(ahfl_runtime_engine PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_engine)

add_library(ahfl_runtime_engine STATIC
    execution_event.cpp
    execution_metadata.cpp
    execution_otel.cpp
    execution_projection.cpp
    execution_renderer.cpp
    execution_report.cpp
    agent_runtime.cpp
    workflow_runtime.cpp
    workflow_recovery.cpp
    capability_bridge.cpp
    capability_eval.cpp
    capability_transport_adapter.cpp
    native_host_binding.cpp
    standard_capabilities.cpp
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
    core_wire_codec.cpp
    core_wasm_schema_transport.cpp
    core_wasm_resume_record.cpp
    core_wasm_schema_module.cpp
    core_wasm_node_events.cpp
    core_wire_canonical_size.cpp
    core_wasm_resume_controller.cpp
    core_wasm_resume_host_codes.cpp
    core_wasm_resume_capacity.cpp
    core_wasm_idempotency_token.cpp
    host_event_envelope.cpp
    durable_effect_intent.cpp
    durable_effect_authority.cpp
    payload_store_codec.cpp
    payload_store.cpp
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

# RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed runtime FACADE.
#
# This target is the peer-tier component that compiles AHFL-IR to wasm and
# drives the wasm_host engine-session layer. It is the ONLY runtime-tier
# target allowed to link a compiler backend (ahfl_compiler_backend_wasm);
# the wasm_host target stays backend-free (grep-zero) so it can be linked
# by consumers that already have compiled modules in hand.
#
# Two classes:
#   * WasmWorkflowRuntime -- compiles every workflow in the program once in
#     the constructor, then run(name, input) drives the wasm_host workflow
#     session. Mirrors the evaluator-backed WorkflowRuntime surface.
#   * WasmAgentRunner -- one-shot compile + run for a single agent.
#
# Gated with AHFL_ENABLE_BACKEND_WASM (the wasm emitter it consumes).
add_library(ahfl_runtime_wasm_runner STATIC
    wasm_workflow_runtime.cpp
    wasm_agent_runner.cpp
)

target_link_libraries(ahfl_runtime_wasm_runner
    PUBLIC
        # The wasm_host engine-session layer (run_workflow_session /
        # run_wasm_agent). The facade delegates to these free functions.
        ahfl_runtime_wasm_host
        # The neutral WorkflowResult / execution-event types.
        ahfl_runtime_engine
    PRIVATE
        # The wasm emitter (emit_core_wasm). This is the ONLY runtime-tier
        # target that links a compiler backend (D-A).
        ahfl_compiler_backend_wasm
        # The IR types (ir::Program, CoreProgram, CoreLayoutTable).
        ahfl_compiler_ir
        # The Value type + wire JSON serialization.
        ahfl_runtime_value
)

# The facade headers include src-internal headers (runtime/wasm_host/*.hpp,
# runtime/engine/workflow_result.hpp); propagate the build-tree src/ dir.
target_include_directories(ahfl_runtime_wasm_runner PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

ahfl_apply_project_warnings(ahfl_runtime_wasm_runner)

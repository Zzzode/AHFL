# RFC 0026 KR6.8 WH-1: the wasm3-backed CoreWasmResumeEngine port. This target
# exists only when AHFL_ENABLE_BACKEND_WASM is ON (the embedded host consumes
# emitted Core-IR wasm; WASM=OFF leaves the tree without it). It is a peer
# execution engine of src/runtime/engine, not a subfolder of it (decision doc
# section 5 tier-honesty), which is why it gets its own directory and target.
#
# WH-2 adds the P6-frame packer + reader + thin driver (frame_packer.cpp,
# frame_reader.cpp, p6_frame_driver.cpp). The packer/reader are pure span
# transforms over the admitted layout section + wire binding: they include no
# wasm3 header and touch no wasm3 type, so wasm3 stays PRIVATE (only
# wasm3_engine.cpp includes wasm3.h). The driver composes the engine + packer +
# reader; it includes wasm3_engine.hpp (which includes the engine port header)
# but not wasm3.h itself.
#
# WH-3 adds the capability-import executor (capability_import.cpp): the
# production ImportCallback the engine delivers every ahfl_cap import to. It
# composes the engine (alloc_then_write / mutable_whole_memory), the packer +
# reader (bridge lane P4-D walk), and the wire-value SSOT (opaque lane JSON
# envelope), and invokes capabilities through the ContextualCapabilityInvoker.
#
# WH-4 adds the state-trace decoder (state_trace_decoder.cpp), the observation
# emitter (observation_emitter.cpp), the agent step-walk session
# (agent_session.cpp), the workflow session (workflow_session.cpp), and the
# wasm3-backed facade (wasm_agent_runner.cpp). The decoder is the host-side
# reader for the guest-written state-entry trace ring (u32 count + 8-byte
# (runner,state) records at the dynamic frame-section coords); the emitter
# produces the canonical ahfl.node-observation.v1 document the conformance
# comparator consumes; the agent session drives a separate effects-free wasm3
# instance through step()/current_state/transition_count to collect the
# state-entry sequence; the workflow session runs a WORKFLOW module end-to-end
# on the canonical instance, wrapping the WH-3 capability_import executor with
# the D1 hook-firing discipline (import-boundary state_entered + post-run
# node_completed); the agent runner drives an AGENT module with the D1
# dual-mode discipline (step-walk on a separate instance + canonical runv/run2);
# the workflow runtime is a thin facade over the workflow session that accepts
# the shared WasmRuntimeHooks (hook signatures identical to WorkflowRuntimeConfig).
#
# WH-4b adds the suspended-snapshot recorder (wasm_resume_recorder.cpp): the
# session-local state holder the workflow session wraps the WH-3 import callback
# with to originate the recovery snapshot (per-node import ordinals, ExactSidecar
# memo, pending coordinate) on a suspend run and to classify + serve the memo /
# inject the frontier on a resume run. It is pure state: the session wrapper owns
# the engine / binding / diagnostic machinery and drives the recorder.
add_library(ahfl_runtime_wasm_host STATIC
    wasm3_engine.cpp
    frame_packer.cpp
    frame_reader.cpp
    p6_frame_driver.cpp
    capability_import.cpp
    state_trace_decoder.cpp
    observation_emitter.cpp
    agent_session.cpp
    workflow_session.cpp
    wasm_resume_recorder.cpp
    wasm_agent_runner.cpp
    wasm_lifecycle.cpp
)

target_link_libraries(ahfl_runtime_wasm_host
    PRIVATE
        # The vendored wasm3 interpreter (C library; SYSTEM include, extern "C"
        # in the TU). Gated with this target by AHFL_ENABLE_BACKEND_WASM.
        wasm3
        # The engine port + the fixed-page capacity authority.
        ahfl_runtime_engine
        # The host wire-value SSOT (Value, value_to_json). The packer/reader
        # headers expose Value in their interface, but the wasm_host target's
        # BUILD_INTERFACE src/ include already makes value.hpp findable; the
        # link dependency is PRIVATE because consumers that construct Values
        # link ahfl_runtime_value explicitly (mirrors the WH-1 test pattern).
        ahfl_runtime_value
        # The F1 ABI SSOT (kCoreWasmFixedLinearMemory* constants) + the P4-D
        # layout / wire-schema IR types the packer/reader walk.
        ahfl_compiler_ir
)

# The engine header includes the engine-port header under src/
# ("runtime/engine/core_wasm_resume_engine.hpp"); propagate the build-tree src/
# dir to consumers. BUILD_INTERFACE only: these headers are NOT installed SDK
# surface (mirrors src/runtime/value/CMakeLists.txt).
target_include_directories(ahfl_runtime_wasm_host PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

ahfl_apply_project_warnings(ahfl_runtime_wasm_host)

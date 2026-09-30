#pragma once

// RFC 0026 KR6.8 WH-4: the wasm3-backed agent runner. Runs an AGENT module
// end-to-end on the wasm3 engine with the D1 dual-mode discipline:
//
//   1. A SEPARATE fresh effects-free wasm3 instance drives
//      step/current_state/transition_count (the agent_session step-walk).
//      state_entered_hook fires LIVE per host-driven step. Effects-replies on
//      this instance are discarded.
//   2. The CANONICAL instance runs runv (P6-frame) or run2 (WireJson) exactly
//      once with the real executor. capability_invoked_hook /
//      capability_result_observer fire LIVE at its imports.
//
// The hook signatures are IDENTICAL to WorkflowRuntimeConfig (see
// wasm_runtime_hooks.hpp).

#include "runtime/wasm_host/agent_session.hpp"
#include "runtime/wasm_host/observation_emitter.hpp"
#include "runtime/wasm_host/wasm_runtime_hooks.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/workflow_result.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ahfl::runtime::wasm_host {

// The result of a successful agent run.
struct WasmAgentRunResult {
    WorkflowResult result;
    // Observation data for the emitter.
    std::vector<StateEntry> states;
    std::vector<std::string> capabilities;
    std::vector<std::string> capability_arguments;
    std::uint32_t transition_count{0};
    // Per-call capability failure kinds (empty when all calls succeed).
    std::vector<CapabilityFailureKind> capability_failures;
};

// Run an AGENT module end-to-end on the wasm3 engine.
//
// `module_bytes` is the compiled wasm module. `descriptor` carries the
// agent's state table, frame section, and wire schema. `input` is the agent
// input. `hooks` carry the debug/test hooks. `invoker` dispatches capability
// calls.
//
// Returns the run result (status, output, observation data, failure kinds),
// or a human-readable error string on failure.
[[nodiscard]] std::expected<WasmAgentRunResult, std::string>
run_wasm_agent(std::span<const std::uint8_t> module_bytes,
               const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
               const Value &input, WasmRuntimeHooks hooks,
               ContextualCapabilityInvoker invoker);

} // namespace ahfl::runtime::wasm_host

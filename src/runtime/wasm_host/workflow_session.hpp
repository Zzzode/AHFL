#pragma once

// RFC 0026 KR6.8 WH-4: the workflow session.
//
// Runs a WORKFLOW module end-to-end on the wasm3 engine, wrapping the WH-3
// capability_import executor with the D1 hook-firing discipline:
//
//   * At EACH import boundary: decode the trace-ring PREFIX from whole_memory
//     and fire state_entered_hook per new record (import-boundary live), then
//     capability_invoked_hook PRE-call.
//   * After run2 returns: decode the FULL trace + node-event buffer, fire
//     state_entered_hook for the remaining records, and fire
//     node_completed_hook per node in schedule order.
//   * agent_input_hook is NOT fired for workflow nodes (in-guest materialized
//     input is not host-observable).
//
// The session collects the observation data (states, capabilities, arguments,
// transition_count, workflow_completed_count) for the observation emitter,
// and the per-call CapabilityFailureKind vector for the facade.

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/wasm_host/observation_emitter.hpp"
#include "runtime/wasm_host/state_trace_decoder.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/runtime/execution_event.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime::wasm_host {

// The hook fired per state entry on the effects-free instance (agent) or at
// import boundaries / post-run (workflow).
using StateEnteredHook =
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, std::string_view state_name)>;

// Configuration for the workflow session. All references must outlive the
// session (the caller owns them).
struct WorkflowSessionConfig {
    // Debug/test hooks (same signatures as WorkflowRuntimeConfig).
    StateEnteredHook state_entered_hook;
    std::function<void(AgentId, std::string_view)> capability_invoked_hook;
    std::function<void(const CapabilityInvocationContext &,
                       const CapabilityCallResult &)>
        capability_result_observer;
    std::function<void(AgentId, std::string_view node_name, const Value &)>
        node_completed_hook;

    // The contextual capability invoker (production host capability dispatch).
    ContextualCapabilityInvoker invoker;

    // Resolves a capability's source_symbol to its canonical name.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;
};

// The result of a successful workflow session run.
struct WorkflowSessionResult {
    WorkflowResult result;
    // Observation data for the emitter.
    std::vector<StateEntry> states;
    std::vector<std::string> capabilities;
    std::vector<std::string> capability_arguments;
    std::uint32_t transition_count{0};
    std::uint32_t workflow_completed_count{0};
    // Per-call capability failure kinds (empty when all calls succeed).
    std::vector<CapabilityFailureKind> capability_failures;
};

// Run a workflow module end-to-end on the wasm3 engine.
//
// `module_bytes` is the compiled wasm module. `descriptor` carries the
// workflow schedule, agents, frame section, and wire schema. `input` is the
// workflow input. `config` carries the hooks and capability invoker.
//
// Returns the session result (status, output, observation data, failure
// kinds), or a human-readable error string on failure.
[[nodiscard]] std::expected<WorkflowSessionResult, std::string>
run_workflow_session(std::span<const std::uint8_t> module_bytes,
                     const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
                     const Value &input, WorkflowSessionConfig config);

} // namespace ahfl::runtime::wasm_host

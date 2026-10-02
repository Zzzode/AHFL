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
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/wasm_host/observation_emitter.hpp"
#include "runtime/wasm_host/state_trace_decoder.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/compiler/ir/expr.hpp"
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

    // WH-5c.5 test seam: invoked AFTER run2 returns and its outcome is
    // classified, BEFORE the post-run per-node output join (section 10).
    // Receives a mutable span of the whole linear memory so a fail-closed
    // mutation pin can corrupt the per-node output stash table or the output
    // JSON bytes and assert the host decodes the corruption as
    // kOutputDecodeFailed (never a silent NoneValue). Production code never
    // sets this.
    std::function<void(std::span<std::uint8_t>)> post_run2_memory_mutator;

    // The contextual capability invoker (production host capability dispatch).
    ContextualCapabilityInvoker invoker;

    // Resolves a capability's source_symbol to its canonical name.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;

    // WH-5c.6: optional failure-diagnostic range resolvers (design
    // 12.15.17.1). The facade pre-computes host-side range tables from the
    // ir::Program and installs these so a run failure (trap / host-abort /
    // output-decode) carries a SourceRange in its diagnostic (Principle 5).
    // node_range_resolver maps a schedule position to the failed node's
    // WorkflowNode::source_range; capability_range_resolver maps a
    // source_symbol to the capability declaration's provenance range. A null
    // resolver is safe: every call site guards `if (config.x_resolver)`.
    std::function<ahfl::ir::SourceRangeOpt(std::uint32_t schedule_pos)>
        node_range_resolver;
    std::function<ahfl::ir::SourceRangeOpt(std::uint64_t source_symbol)>
        capability_range_resolver;

    // WH-4b: recovery snapshot for a resume run. When set, the session loads
    // the snapshot's memo + pending frontier into its recorder and replays:
    // memo-supply calls before the frontier, inject the pending result at the
    // frontier, and go live after it (ReadyForLive). The invoker is never
    // called for a memo hit or the frontier, so a resumed run has zero live
    // side effects before the pending call.
    std::optional<WorkflowRecoverySnapshot> recovery_snapshot;

    // WH-4b: the store to persist the recovery snapshot on suspend. Null means
    // no persistence: a suspend downgrades to NodeFailed (mirrors the evaluator
    // at workflow_runtime.cpp:1582-1603).
    const WorkflowRecoveryStore *recovery_store{nullptr};

    // WH-4b: the pending capability result for a resume run, as a native Value.
    // Mutually exclusive with resume_pending_result_wire_json. The wasm lane
    // serializes it via the canonical wire encoder before injecting.
    std::optional<Value> resume_pending_result;

    // WH-4b: the pending capability result for a resume run, as raw wire JSON.
    // Mutually exclusive with resume_pending_result. The wasm lane parses +
    // schema-decodes it under the pending capability's result binding (type
    // gate) and then supplies the ORIGINAL bytes verbatim.
    std::optional<std::string> resume_pending_result_wire_json;
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

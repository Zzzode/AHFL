#pragma once

// RFC 0026 KR6.8 WH-4 fix-forward P1-2/P1-3: the shared lifecycle helper for
// the wasm3-backed execution lanes. Both the workflow session
// (workflow_session.cpp) and the agent runner (wasm_agent_runner.cpp) use
// this to populate the ExecutionMetadataStore, emit the lifecycle event
// stream, and build the ExecutionReport from those events via
// build_execution_report -- the SAME projection the evaluator-backed
// WorkflowRuntime uses (finalize_report in workflow_runtime.cpp). No
// hand-populated report path survives: the report is ALWAYS a projection of
// the event stream, so the replay/audit projections in execution_renderer
// produce correct counts on both lanes.

#include "runtime/engine/workflow_result.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/runtime/execution_event.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ahfl::runtime::wasm_host {

// One state entry for a node: the runner index (into
// CoreWasmExecutionDescriptor::agents) and the state name.
struct WasmNodeStateEntry {
    std::uint32_t runner;
    std::string state_name;
};

// Per-node run facts, in schedule order (descriptor.nodes order).
struct WasmNodeRunFacts {
    std::vector<WasmNodeStateEntry> states;
    // Node output. P6-frame: decoded from the runner's O_k block. WireJson:
    // nullopt (individual node outputs are not host-observable on the opaque
    // lane; the module passes them through its own heap).
    std::optional<Value> output;
    // The node terminal. Completed = NodeCompleted, Failed = NodeFailed,
    // Skipped = NodeSkipped (never executed because a prior node failed),
    // Suspended = NodeSuspended (suspended on a pending capability call).
    enum class Terminal { Completed, Failed, Skipped, Suspended };
    Terminal terminal{Terminal::Completed};
    NodeFailureKind failure_kind{NodeFailureKind::AgentFailed};
    // For Failed: the diagnostic code + message (the helper adds it to the
    // result's DiagnosticBag and references it from NodeFailed).
    std::string failure_code;
    std::string failure_message;
    // WH-5c.6: the SourceRange attached to the failure diagnostic (the
    // failed node's WorkflowNode::source_range, or the capability
    // declaration range when a capability failed). Nullopt when no resolver
    // is installed or the lookup missed.
    ahfl::ir::SourceRangeOpt failure_range;
    // For Skipped: blocking dependency node_ids (dense source-order).
    std::vector<std::uint32_t> blocking_dependencies;
    // WH-4b: for Suspended, the pending capability identity + per-node ordinal
    // (carried into the NodeSuspended event).
    std::size_t pending_cap_id{0};
    std::uint64_t pending_ordinal{0};
};

// One capability call observed during the run, in call order. The workflow
// session collects these from the import callback; the lifecycle helper
// projects the aggregate CapabilityCallResult into the event stream via the
// shared capability_event_projection helper (the SAME synthesis the
// evaluator uses), between NodeStarted and NodeCompleted for the calling
// node. node_id is the descriptor's dense source-order node id (NOT the
// WorkflowNodeId assigned by the metadata store); the lifecycle helper
// resolves it via plan.node_by_id.
//
// WH-5c.8: collapsed to {node_id, capability_name, result} — success /
// output / attempts / cache_hit / usage all derive from the aggregate
// CapabilityCallResult, which the shared projection helper reads directly.
// A Pending call is NOT collected (it has no terminal yet; the node-level
// NodeSuspended terminal records the pause).
struct WasmCapabilityCall {
    std::uint32_t node_id;
    std::string capability_name;
    CapabilityCallResult result;
};

// All facts for one wasm workflow lane run.
//
// WH-4b: a Suspended run sets status=RunTerminalStatus::Suspended, the
// suspended node's facts to Terminal::Suspended (with pending_cap_id +
// pending_ordinal), and every node after it in schedule order to
// Terminal::Skipped (blocking dependency = the suspended node). The helper
// emits NodeSuspended / WorkflowSuspended / RunCompleted{Suspended}, matching
// the evaluator's suspend terminal (workflow_runtime.cpp:1585-1591).
struct WasmWorkflowRunFacts {
    std::vector<WasmNodeRunFacts> nodes; // schedule order
    std::vector<WasmCapabilityCall> capability_calls; // call order
    std::optional<Value> workflow_output;
    RunTerminalStatus status{RunTerminalStatus::Completed};
    std::optional<WorkflowFailureKind> failure_kind;
    // For workflow-level failure: the diagnostic code + message.
    std::string failure_code;
    std::string failure_message;
    // WH-5c.6: the SourceRange attached to the failure diagnostic.
    ahfl::ir::SourceRangeOpt failure_range;
    // WH-5c.6: the DiagnosticId of the failed node's NodeFailed diagnostic,
    // so the workflow-level WorkflowFailed reuses it (one bag entry
    // referenced by both events, mirroring the evaluator at
    // workflow_runtime.cpp:1594-1597). Nullopt when no node failed.
    std::optional<DiagnosticId> node_failure_diagnostic;
};

// Populate metadata from the descriptor, emit the lifecycle event stream
// (RunStarted / WorkflowStarted / NodeScheduled / NodeStarted /
// AgentStateEntered / NodeCompleted|NodeFailed|NodeSkipped|NodeSuspended /
// WorkflowCompleted|WorkflowFailed|WorkflowSuspended / RunCompleted), and
// build the report from those events via build_execution_report.
//
// AgentId assignment matches the evaluator's build_runtime_plan:
// first-appearance by agent NAME in node_id (source) order, NOT runner
// order. WorkflowNodeId is assigned in node_id order so it equals the
// evaluator's dense source-order index. NodeScheduled is emitted in
// schedule (Kahn) order with dependencies + execution_slot, matching the
// evaluator.
//
// Returns false if the event stream violated the lifecycle contract (a
// diagnostic is emitted and the report is left in a failed state).
[[nodiscard]] bool finalize_wasm_workflow_run(
    WorkflowResult &result,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    WasmWorkflowRunFacts facts);

// Populate metadata + emit lifecycle events + build report for an AGENT run.
// The agent lane wraps the bare agent in a synthetic single-node workflow
// (workflow + node named after the agent). walk_states are the state names
// in entry order (from the effects-free step-walk). capability_calls are
// the capability calls observed during the run (in call order); the helper
// emits CapabilityStarted / CapabilityCompleted events for each, matching
// the evaluator's event order (between AgentStateEntered and the node
// terminal).
[[nodiscard]] bool finalize_wasm_agent_run(
    WorkflowResult &result,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    const std::vector<std::string> &walk_states,
    std::vector<WasmCapabilityCall> capability_calls,
    std::optional<Value> output,
    RunTerminalStatus status,
    std::optional<WorkflowFailureKind> failure_kind,
    std::string failure_code,
    std::string failure_message);

} // namespace ahfl::runtime::wasm_host

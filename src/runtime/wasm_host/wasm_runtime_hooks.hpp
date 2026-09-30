#pragma once

// RFC 0026 KR6.8 WH-4: the shared hook configuration for the wasm3-backed
// runtime facade. The hook signatures are IDENTICAL to WorkflowRuntimeConfig
// (src/runtime/engine/workflow_runtime.hpp), so a host that already drives the
// evaluator-backed WorkflowRuntime through these hooks can switch to the wasm3
// facade without changing its hook wiring. The facade fires the same hooks at
// the same semantic points:
//
//   * state_entered_hook   -- per state entry (agent: step-walk on the
//                             effects-free instance; workflow: trace-ring
//                             import-boundary + post-run)
//   * agent_input_hook     -- agent lane only, LIVE before the step-walk,
//                             with the agent's input Value
//   * capability_invoked_hook  -- PRE-call, before the capability invoker
//   * capability_result_observer -- POST-call, after the invoker returns
//   * node_completed_hook  -- per node, after the run completes (workflow only)

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/runtime/execution_event.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::runtime::wasm_host {

// The hook set shared by WasmAgentRunner and WasmWorkflowRuntime. Every hook
// is optional; a null hook is simply not fired.
struct WasmRuntimeHooks {
    // Debug/test hook invoked after the runtime records an agent state entry.
    // `agent_name` / `node_name` are the canonical agent name and the workflow
    // node name executing it (empty for import-boundary workflow fires, where
    // the node is not host-observable).
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, std::string_view state_name)>
        state_entered_hook;

    // Debug/test hook invoked LIVE before the agent step-walk with the
    // agent's input Value (agent lane only; not fired for workflow nodes,
    // whose in-guest materialized input is not host-observable).
    std::function<void(AgentId, std::string_view agent_name, const Value &)>
        agent_input_hook;

    // Debug/test hook invoked right before a capability call is dispatched.
    std::function<void(AgentId, std::string_view)> capability_invoked_hook;

    // Debug/test hook invoked after a capability call returns.
    std::function<void(const CapabilityInvocationContext &,
                       const CapabilityCallResult &)>
        capability_result_observer;

    // Debug/test hook invoked with a node's output after the node completes
    // (workflow only; not fired for bare agents).
    std::function<void(AgentId, std::string_view node_name, const Value &)>
        node_completed_hook;
};

} // namespace ahfl::runtime::wasm_host

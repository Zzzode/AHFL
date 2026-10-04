#pragma once

// RFC 0026 KR6.8 WH-4: the shared hook configuration for the wasm3-backed
// runtime.
//
// Firing-point semantics (where the wasm lane fires each hook):
//   * state_entered_hook   -- per state entry (agent: step-walk on the
//                             effects-free instance; workflow: POST-RUN
//                             schedule-ordered full sequence, the observation
//                             channel -- WH-5c.4 251ebdb7)
//   * state_entered_live_hook -- workflow P6 lane ONLY: import-boundary
//                             trace-prefix LIVE fire (the debug channel,
//                             kr68 section 12.9.14). Never WireJson (no trace
//                             ring), never post-run, never from the agent
//                             runner (whose state_entered_hook is already
//                             live per step). A host that wants schedule-
//                             ordered observation installs state_entered_hook;
//                             a host that wants live (pre-capability) state
//                             observation installs state_entered_live_hook.
//                             The DAP installs both and deduplicates by
//                             (node, state, occurrence-within-node).
//   * agent_input_hook     -- agent lane only, LIVE before the step-walk,
//                             with the agent's input Value. `node_name` is
//                             ALWAYS empty on the wasm agent lane (a bare agent
//                             has no workflow node); the workflow session never
//                             fires it (in-guest materialized node input is not
//                             host-observable). The wasm agent lane cannot
//                             supply a node name, so the parameter is always
//                             empty on this lane.
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
    // node name executing it. On the workflow lane this is the POST-RUN
    // schedule-ordered full sequence (the observation channel, WH-5c.4
    // 251ebdb7); on the agent lane it is LIVE per step.
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, std::string_view state_name)>
        state_entered_hook;

    // Debug hook invoked LIVE on the workflow P6 lane at each capability
    // import boundary, firing the trace-ring PREFIX (new records since the
    // last boundary) BEFORE the capability is dispatched (kr68
    // section 12.9.14). This is the debug channel: a blocking hook pauses the
    // run genuinely inside the ImportCallback, before the capability's side
    // effects. NEVER fired on the WireJson lane (no trace ring), NEVER
    // post-run (the remainder is by definition post-mortem), and NEVER from
    // the agent runner (whose state_entered_hook is already live per step).
    // Signature is byte-identical to state_entered_hook.
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, std::string_view state_name)>
        state_entered_live_hook;

    // Debug/test hook invoked LIVE before the agent step-walk with the
    // agent's input Value (agent lane only; not fired for workflow nodes,
    // whose in-guest materialized input is not host-observable). `node_name`
    // is always empty on the wasm agent lane (see the header comment).
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, const Value &)>
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

#pragma once

// RFC 0026 KR6.8 WH-4: the agent step-walk session.
//
// Drives a SEPARATE effects-free wasm3 instance through step()/current_state/
// transition_count to collect the state-entry sequence, mirroring the JS
// oracle's collectStatesViaStep (node_embedded_host.mjs:1035-1065). The
// canonical instance runs runv/run2 exactly once for effects + output; this
// session owns ONLY the state walk.
//
// The bounded-walk guard (states.size() + 2), current_state consistency, and
// exactly-once transition_count bump are replicated from the oracle. The
// state_entered_hook fires LIVE per step() transition on the effects-free
// instance -- host-driven, exact, so the DAP's pause/step semantics map 1:1.
//
// For P6-frame agents the input is packed into the fixed P4-D regions before
// the walk (the computed handlers branch on packed input). For WireJson agents
// no packing is needed: step() drives the state machine without the input,
// which run2 consumes separately.

#include "runtime/wasm_host/wasm3_engine.hpp"
#include "runtime/value/value.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/execution_event.hpp"

namespace ahfl::runtime::wasm_host {

// The descriptor the session walks against. Only the agent-lane fields are
// consumed; the workflow-lane fields (agents, nodes) are ignored.
struct AgentWalkDescriptor {
    std::string agent_name;
    std::vector<std::string> states; // id -> state name
    std::uint32_t initial_state{0};
    bool is_p6_frame{false};
    // P6-frame packing authorities (populated iff is_p6_frame).
    const ahfl::ir::core::CoreFrameLayoutSection *frame_section{nullptr};
    const ahfl::ir::core::CoreWireSchemaTable *wire_schema{nullptr};
};

// The hook fired per step() transition on the effects-free instance.
using StateEnteredHook =
    std::function<void(AgentId, std::string_view agent_name,
                       std::string_view node_name, std::string_view state_name)>;

// The result of a successful step-walk.
struct AgentWalkResult {
    std::vector<std::string> states; // state names in walk order (incl. initial)
    std::uint32_t transition_count{0};
};

// Drive the step-walk on a fresh effects-free wasm3 instance.
//
// `module_bytes` is the compiled wasm module. `descriptor` carries the state
// name table and (for P6-frame) the packing authorities. `input` is the agent
// input (packed for P6-frame, ignored for WireJson). `import_callback` serves
// capability imports on the effects-free instance: its results drive the state
// walk but its events are discarded (the canonical instance fires the real
// capability hooks). `hook` fires per transition. `agent_id`/`node_name` are
// passed through to the hook.
//
// Returns the walked state sequence (including the initial state) and the
// final transition_count, or a human-readable error string on failure.
[[nodiscard]] std::expected<AgentWalkResult, std::string>
run_agent_step_walk(std::span<const std::uint8_t> module_bytes,
                    const AgentWalkDescriptor &descriptor, const Value &input,
                    core_wasm_resume_engine::ImportCallback import_callback,
                    StateEnteredHook hook, AgentId agent_id,
                    std::string_view node_name);

} // namespace ahfl::runtime::wasm_host

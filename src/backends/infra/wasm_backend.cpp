#include "backends/infra/wasm_backend.hpp"

#include <algorithm>
#include <unordered_map>

namespace ahfl::backends {

std::string emit_wat_header(const std::string &module_name) {
    std::string wat;
    wat += "(module $" + module_name + "\n";
    wat += "  ;; Type declarations\n";
    wat += "  (type $state_fn (func (param i32) (result i32)))\n";
    wat += "  (type $void_fn (func))\n";
    wat += "  (type $cap_fn (func (param i32) (result i32)))\n";
    wat += "\n";
    wat += "  ;; Linear memory for state storage and context\n";
    wat += "  (memory (export \"memory\") 1)\n";
    wat += "\n";
    wat += "  ;; Global mutable state: current state index\n";
    wat += "  (global $current_state (mut i32) (i32.const 0))\n";
    wat += "  ;; Transition count for quota enforcement\n";
    wat += "  (global $transition_count (mut i32) (i32.const 0))\n";
    wat += "\n";
    return wat;
}

std::string emit_wat_state_table(const std::vector<std::string> &states) {
    std::string wat;
    wat += "  ;; ================================================================\n";
    wat += "  ;; State encoding: " + std::to_string(states.size()) + " states\n";
    wat += "  ;; ================================================================\n";
    for (std::size_t i = 0; i < states.size(); ++i) {
        wat += "  ;; state " + std::to_string(i) + " = " + states[i] + "\n";
    }
    wat += "\n";

    // Store state name table in data section for introspection
    wat += "  ;; State name data (for runtime introspection)\n";
    std::size_t offset = 0;
    for (std::size_t i = 0; i < states.size(); ++i) {
        wat += "  (data (i32.const " + std::to_string(offset) + ") \"" + states[i] + "\\00\")\n";
        offset += states[i].size() + 1;
    }
    wat += "\n";
    return wat;
}

namespace {

/// Build a state-index lookup from state names.
std::unordered_map<std::string, std::size_t>
build_state_index(const std::vector<std::string> &states) {
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < states.size(); ++i) {
        index[states[i]] = i;
    }
    return index;
}

/// Group transitions by source state for the dispatch table.
struct TransitionGroup {
    std::size_t from_index;
    std::vector<std::size_t> to_indices; // one per event_id ordinal
};

/// Emit the core transition function using br_table for O(1) dispatch.
/// Model: transition(event_id: i32) -> new_state: i32
///
/// The function uses a two-level dispatch:
///   1. Outer br_table dispatches on current_state
///   2. Inner br_table (per state) dispatches on event_id to select target state
std::string
emit_transition_function(const std::vector<std::string> &states,
                         const std::vector<std::pair<std::string, std::string>> &transitions,
                         const std::unordered_map<std::string, std::size_t> &state_index) {

    // Build per-source transition map: from_state_idx -> [(event_idx, to_state_idx)]
    // Event ID = ordinal of the transition in the list
    std::unordered_map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> trans_map;
    for (std::size_t event_id = 0; event_id < transitions.size(); ++event_id) {
        const auto &[from, to] = transitions[event_id];
        auto from_it = state_index.find(from);
        auto to_it = state_index.find(to);
        if (from_it != state_index.end() && to_it != state_index.end()) {
            trans_map[from_it->second].emplace_back(event_id, to_it->second);
        }
    }

    std::string wat;
    wat += "  ;; ================================================================\n";
    wat += "  ;; Core transition function\n";
    wat += "  ;; Input: event_id (i32) — indexes into the transition table\n";
    wat += "  ;; Returns: new state index (i32), or current state if invalid\n";
    wat += "  ;; Side effects: updates $current_state, increments $transition_count\n";
    wat += "  ;; ================================================================\n";
    wat += "  (func $transition (export \"transition\") (param $event_id i32) (result i32)\n";
    wat += "    (local $state i32)\n";
    wat += "    (local $target i32)\n";
    wat += "\n";
    wat += "    ;; Load current state\n";
    wat += "    (local.set $state (global.get $current_state))\n";
    wat += "    ;; Default target = stay in current state (invalid transition)\n";
    wat += "    (local.set $target (local.get $state))\n";
    wat += "\n";

    // Outer dispatch: switch on current state
    wat += "    ;; Dispatch on current state\n";
    wat += "    (block $done\n";

    // Create a block per state for the br_table to jump to
    for (std::size_t i = 0; i < states.size(); ++i) {
        wat += "      (block $state_" + std::to_string(i) + "\n";
    }

    // br_table instruction: jump to block based on state index
    wat += "        (br_table";
    for (std::size_t i = 0; i < states.size(); ++i) {
        wat += " $state_" + std::to_string(i);
    }
    wat += " $done"; // default: jump to done (invalid state)
    wat += " (local.get $state))\n";

    // Close blocks in reverse and add per-state transition logic
    for (std::size_t i = states.size(); i > 0; --i) {
        std::size_t state_idx = i - 1;
        wat +=
            "      ) ;; end $state_" + std::to_string(state_idx) + " (" + states[state_idx] + ")\n";

        // Per-state event dispatch
        auto it = trans_map.find(state_idx);
        if (it != trans_map.end() && !it->second.empty()) {
            const auto &state_transitions = it->second;

            // Find max event_id for this state's br_table sizing
            std::size_t max_event = 0;
            for (const auto &[eid, _] : state_transitions) {
                max_event = std::max(max_event, eid);
            }

            // Build event → target mapping array
            std::vector<std::size_t> event_targets(max_event + 1, state_idx); // default: stay
            for (const auto &[eid, tid] : state_transitions) {
                event_targets[eid] = tid;
            }

            // Use if-else chain for clarity and correctness (br_table needs
            // contiguous labels which are complex for sparse event spaces)
            for (const auto &[eid, tid] : state_transitions) {
                wat += "      (if (i32.eq (local.get $event_id) (i32.const " + std::to_string(eid) +
                       "))\n";
                wat +=
                    "        (then (local.set $target (i32.const " + std::to_string(tid) + "))))\n";
            }
        }
        wat += "      (br $done)\n";
    }

    wat += "    ) ;; end $done\n";
    wat += "\n";
    wat += "    ;; Commit state transition if target differs\n";
    wat += "    (if (i32.ne (local.get $target) (local.get $state))\n";
    wat += "      (then\n";
    wat += "        (global.set $current_state (local.get $target))\n";
    wat += "        (global.set $transition_count\n";
    wat += "          (i32.add (global.get $transition_count) (i32.const 1)))))\n";
    wat += "\n";
    wat += "    ;; Return new state\n";
    wat += "    (local.get $target)\n";
    wat += "  )\n";
    return wat;
}

/// Emit helper functions: get_state, is_final, get_transition_count, reset.
std::string emit_helper_functions(const std::vector<std::string> &states,
                                  const std::vector<std::string> &final_states,
                                  const std::unordered_map<std::string, std::size_t> &state_index) {

    std::string wat;

    // get_state: returns current state index
    wat += "\n";
    wat += "  ;; Get current state index\n";
    wat += "  (func $get_state (export \"get_state\") (result i32)\n";
    wat += "    (global.get $current_state)\n";
    wat += "  )\n";

    // is_final: returns 1 if current state is a final state
    wat += "\n";
    wat += "  ;; Check if current state is a final (accepting) state\n";
    wat += "  (func $is_final (export \"is_final\") (result i32)\n";
    if (final_states.empty()) {
        wat += "    (i32.const 0)\n";
    } else {
        wat += "    (local $state i32)\n";
        wat += "    (local.set $state (global.get $current_state))\n";
        // Check each final state
        std::string condition;
        for (std::size_t i = 0; i < final_states.size(); ++i) {
            auto it = state_index.find(final_states[i]);
            if (it == state_index.end())
                continue;
            std::string check =
                "(i32.eq (local.get $state) (i32.const " + std::to_string(it->second) + "))";
            if (condition.empty()) {
                condition = check;
            } else {
                condition = "(i32.or " + condition + " " + check + ")";
            }
        }
        if (condition.empty()) {
            wat += "    (i32.const 0)\n";
        } else {
            wat += "    " + condition + "\n";
        }
    }
    wat += "  )\n";

    // get_transition_count
    wat += "\n";
    wat += "  ;; Get total number of transitions executed\n";
    wat += "  (func $get_transition_count (export \"get_transition_count\") (result i32)\n";
    wat += "    (global.get $transition_count)\n";
    wat += "  )\n";

    // reset: reset to initial state
    wat += "\n";
    wat += "  ;; Reset state machine to initial state\n";
    wat += "  (func $reset (export \"reset\")\n";
    wat += "    (global.set $current_state (i32.const 0))\n";
    wat += "    (global.set $transition_count (i32.const 0))\n";
    wat += "  )\n";

    // state_count: returns total number of states
    wat += "\n";
    wat += "  ;; Get total number of states in the machine\n";
    wat += "  (func $state_count (export \"state_count\") (result i32)\n";
    wat += "    (i32.const " + std::to_string(states.size()) + ")\n";
    wat += "  )\n";

    return wat;
}

/// Emit capability import declarations for WASM host bindings.
std::string emit_capability_imports(const std::vector<std::string> &capabilities) {
    if (capabilities.empty())
        return "";
    std::string wat;
    wat += "  ;; Capability imports (host functions)\n";
    for (const auto &cap : capabilities) {
        wat +=
            "  (import \"env\" \"" + cap + "\" (func $cap_" + cap + " (param i32) (result i32)))\n";
    }
    wat += "\n";
    return wat;
}

/// Identify which states are "final" based on having no outgoing transitions.
std::vector<std::string>
infer_final_states(const std::vector<std::string> &states,
                   const std::vector<std::pair<std::string, std::string>> &transitions) {
    std::vector<std::string> finals;
    for (const auto &state : states) {
        bool has_outgoing = false;
        for (const auto &[from, to] : transitions) {
            if (from == state) {
                has_outgoing = true;
                break;
            }
        }
        if (!has_outgoing) {
            finals.push_back(state);
        }
    }
    return finals;
}

} // anonymous namespace

WasmModule generate_wasm(const WasmAgentConfig &config) {
    WasmModule mod;
    mod.module_name = config.agent_name;

    if (config.states.empty()) {
        // Degenerate case: empty state machine — still emit memory and state table comment
        std::string wat;
        wat += "(module $" + config.agent_name + "\n";
        wat += "  ;; Linear memory for state storage and context\n";
        wat += "  (memory (export \"memory\") 1)\n";
        wat += "\n";
        wat += "  ;; state table: 0 states\n";
        wat += ")\n";
        mod.wat_source = wat;
        return mod;
    }

    auto state_index = build_state_index(config.states);
    auto final_states = infer_final_states(config.states, config.transitions);

    std::string wat;
    wat += emit_wat_header(config.agent_name);
    wat += emit_capability_imports(config.capabilities);
    wat += emit_wat_state_table(config.states);
    wat += emit_transition_function(config.states, config.transitions, state_index);
    wat += emit_helper_functions(config.states, final_states, state_index);
    wat += ")\n";

    mod.wat_source = wat;

    // Register exports
    mod.exports.emplace_back("memory");
    mod.exports.emplace_back("get_state");
    mod.exports.emplace_back("transition");
    mod.exports.emplace_back("is_final");
    mod.exports.emplace_back("get_transition_count");
    mod.exports.emplace_back("reset");
    mod.exports.emplace_back("state_count");

    // Register imports
    for (const auto &cap : config.capabilities) {
        mod.imports.emplace_back("env." + cap);
    }

    return mod;
}

} // namespace ahfl::backends

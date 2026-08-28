// ---------------------------------------------------------------------------
// AhflIr -> Core-IR lowering (RFC 0026 slice P3/P4 first increment, KR6.4)
// ---------------------------------------------------------------------------
//
// This is the scaffolded entry point for the execution-layer lowering. See
// `include/ahfl/compiler/ir/core_ir.hpp` for the layer's contract. This
// increment lowers only agent state machines and erases the verification-layer
// constructs (contract / temporal / decreases / quota). The remaining Decl
// kinds and the full lowering (monomorphization, explicit capability-calls,
// structured control flow, memory layout) are filled by later KR6.4 sub-slices.

#include "ahfl/compiler/ir/core_ir.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/mangling.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace ahfl::ir::core {
namespace {

/// Resolve a state name to its index in `names`, appending it if unseen. Keeps
/// state identity index-based (AGENTS.md Principle 2) while tolerating an agent
/// whose `initial` / `final` / transition endpoints name a state not listed in
/// `states` (defensive: still deterministic, still well-formed).
[[nodiscard]] CoreStateId
intern_state(std::vector<std::string> &names,
             std::unordered_map<std::string, std::uint32_t> &index_of,
             const std::string &name) {
    const auto it = index_of.find(name);
    if (it != index_of.end()) {
        return CoreStateId{it->second};
    }
    const auto id = static_cast<std::uint32_t>(names.size());
    names.push_back(name);
    index_of.emplace(name, id);
    return CoreStateId{id};
}

/// Lower one `ir::AgentDecl` state machine into a `CoreAgentDecl`, erasing all
/// verification-layer state (quota / contract / temporal live at `AhflIr`).
[[nodiscard]] CoreAgentDecl lower_agent(const AgentDecl &agent) {
    CoreAgentDecl out;
    out.name = agent.name;
    out.symbol_ref = agent.symbol_ref;

    // Instance key: reuse the canonical monomorphization mangling
    // (mangling.hpp) rather than reinventing it. This increment predates full
    // monomorphization, so the type-arg list is empty (a non-generic instance);
    // generic agents get one CoreAgentDecl per concrete instantiation once the
    // monomorphization sub-slice lands. The mangler is deterministic given the
    // (canonical name, type args) pair.
    const SymbolId symbol{agent.symbol_ref.id.value_or(0)};
    const std::string canonical = agent.symbol_ref.canonical_name;
    const mangle::SymbolCanonicalNameFn resolver =
        [&canonical](SymbolId) -> std::optional<std::string> {
        if (canonical.empty()) {
            return std::nullopt;
        }
        return canonical;
    };
    out.instance_key = mangle::mangle_instance(symbol, /*type_args=*/{}, resolver);

    // States: index-based identity. Seed the table from the declared state set
    // so `CoreStateId` order matches the source declaration order.
    std::unordered_map<std::string, std::uint32_t> index_of;
    out.states.reserve(agent.states.size());
    for (const std::string &state : agent.states) {
        // Seed the table in declaration order; the interned id equals the
        // vector index, so the return value is intentionally discarded here.
        static_cast<void>(intern_state(out.states, index_of, state));
    }

    out.initial = intern_state(out.states, index_of, agent.initial_state);

    out.finals.reserve(agent.final_states.size());
    for (const std::string &fin : agent.final_states) {
        out.finals.push_back(intern_state(out.states, index_of, fin));
    }

    out.transitions.reserve(agent.transitions.size());
    for (const TransitionDecl &t : agent.transitions) {
        out.transitions.push_back(
            CoreTransition{intern_state(out.states, index_of, t.from_state),
                           intern_state(out.states, index_of, t.to_state)});
    }

    // NOTE (RFC 0026): agent.quota / the agent's ContractDecl / temporal
    // properties are intentionally NOT carried here — they are verification
    // constructs consumed at the AhflIr layer and erased at Core-IR.
    return out;
}

} // namespace

CoreProgram lower_ahfl_to_core(const AhflIr &ahfl_ir) {
    CoreProgram core;
    // Visit declarations in source order and emit in that order: deterministic.
    for (const Decl &decl : ahfl_ir.declarations) {
        std::visit(Overloaded{
                       [&](const AgentDecl &agent) {
                           core.declarations.emplace_back(lower_agent(agent));
                       },
                       // Every other Decl kind is skipped in this increment;
                       // its execution-layer lowering is filled by later KR6.4
                       // sub-slices (RFC 0026 Implementation Plan P3-P4).
                       [](const auto &) {},
                   },
                   decl);
    }
    return core;
}

} // namespace ahfl::ir::core

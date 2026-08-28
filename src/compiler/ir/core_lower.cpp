// ---------------------------------------------------------------------------
// AhflIr -> Core-IR lowering (RFC 0026 slice P3/P4 first increment, KR6.4)
// ---------------------------------------------------------------------------
//
// This is the scaffolded entry point for the execution-layer lowering. See
// `include/ahfl/compiler/ir/core_ir.hpp` for the layer's contract. This
// increment lowers agent state machines and capability declarations (effect ->
// explicit capability-call), erasing the verification-layer constructs
// (contract / temporal / decreases / quota) and the orchestration-only slice of
// a capability's effect spec (domain / receipt / retry / … — only the effect
// KIND survives). The remaining Decl kinds and the full lowering
// (monomorphization, structured control flow, memory layout, capability-call
// argument passing) are filled by later KR6.4 sub-slices.

#include "ahfl/compiler/ir/core_ir.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/mangling.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace ahfl::ir::core {
namespace {

/// Deep-copy a structured `ir::TypeRef`. `TypeRef` owns its child refs through
/// `Owned<TypeRef>` (move-only), so carrying a capability's signature into
/// Core-IR needs an explicit recursive clone rather than an assignment. Mirrors
/// the per-TU clone used by the opt lowering (`opt_lower.cpp`); no shared clone
/// helper is exported by the IR headers.
[[nodiscard]] TypeRef clone_type_ref(const TypeRef &type) {
    TypeRef clone;
    clone.kind = type.kind;
    clone.display_name = type.display_name;
    clone.canonical_name = type.canonical_name;
    clone.variant_name = type.variant_name;
    clone.int_bounds = type.int_bounds;
    clone.string_bounds = type.string_bounds;
    clone.decimal_scale = type.decimal_scale;
    clone.collection_capacity = type.collection_capacity;
    clone.source_range = type.source_range;
    if (type.first) {
        clone.first = make_owned<TypeRef>(clone_type_ref(*type.first));
    }
    if (type.second) {
        clone.second = make_owned<TypeRef>(clone_type_ref(*type.second));
    }
    clone.params.reserve(type.params.size());
    for (const auto &param : type.params) {
        clone.params.push_back(param ? make_owned<TypeRef>(clone_type_ref(*param))
                                     : nullptr);
    }
    return clone;
}

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

/// Lower one `ir::CapabilityDecl` into a `CoreCapabilityDecl` — the explicit
/// capability-call / `ahfl_cap` import boundary (RFC 0026 step 2, RFC 0019 /
/// RFC 0020 "computation stays host-side"). Only the execution-relevant shape
/// survives: canonical `symbol_ref` identity, display name, the effect KIND, and
/// the marshalling signature (param types + return type). The rest of the
/// capability's `CapabilityEffectSpec` (domain / idempotency key / receipt /
/// retry / timeout / compensation / policies) is verification / orchestration
/// metadata and is erased at this layer.
[[nodiscard]] CoreCapabilityDecl lower_capability(const CapabilityDecl &cap) {
    CoreCapabilityDecl out;
    out.name = cap.name;
    out.symbol_ref = cap.symbol_ref;
    // Reuse the source effect CATEGORY verbatim; do not reinterpret it.
    out.effect_kind = cap.effect.kind;

    // Marshalling signature: parameter types in declaration order (arity + the
    // concrete types the `ahfl_cap` frame carries). Source-level param NAMES are
    // display-only and are intentionally dropped at the execution layer.
    out.param_types.reserve(cap.params.size());
    for (const ParamDecl &param : cap.params) {
        out.param_types.push_back(clone_type_ref(param.type_ref));
    }
    out.return_type_ref = clone_type_ref(cap.return_type_ref);
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
                       [&](const CapabilityDecl &cap) {
                           core.declarations.emplace_back(lower_capability(cap));
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

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
#include <vector>

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

// ---------------------------------------------------------------------------
// Flow capability call-site extraction
// ---------------------------------------------------------------------------

/// Build a lookup from a capability's canonical identity to its effect kind, so
/// each lowered call site can carry the effect category without a second pass.
/// Keyed by the numeric SymbolId when present (Principle 2), else by canonical
/// name as a fallback (still deterministic).
using EffectKindByName = std::unordered_map<std::string, CapabilityEffectKind>;
using EffectKindById = std::unordered_map<std::size_t, CapabilityEffectKind>;

struct CapabilityEffectIndex {
    EffectKindById by_id;
    EffectKindByName by_name;

    [[nodiscard]] CapabilityEffectKind lookup(const SymbolRef &ref) const {
        if (ref.id.has_value()) {
            const auto it = by_id.find(*ref.id);
            if (it != by_id.end()) {
                return it->second;
            }
        }
        if (!ref.canonical_name.empty()) {
            const auto it = by_name.find(ref.canonical_name);
            if (it != by_name.end()) {
                return it->second;
            }
        }
        // Defensive fallback: an unresolved callee keeps Unknown (still a valid,
        // deterministic call site — the executor treats it as an opaque import).
        return CapabilityEffectKind::Unknown;
    }
};

[[nodiscard]] CapabilityEffectIndex build_effect_index(const AhflIr &ahfl_ir) {
    CapabilityEffectIndex index;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *cap = std::get_if<CapabilityDecl>(&decl)) {
            if (cap->symbol_ref.id.has_value()) {
                index.by_id.emplace(*cap->symbol_ref.id, cap->effect.kind);
            }
            if (!cap->symbol_ref.canonical_name.empty()) {
                index.by_name.emplace(cap->symbol_ref.canonical_name, cap->effect.kind);
            }
        }
    }
    return index;
}

/// Canonical dotted spelling of a path (root + member chain).
[[nodiscard]] std::string path_text(const Path &path) {
    std::string text = path.root_name;
    for (const std::string &member : path.members) {
        text += '.';
        text += member;
    }
    return text;
}

/// Lower one argument expression into a `CoreCallArg`. Only the forms with a
/// direct execution meaning are recognized; anything else becomes a bounded,
/// observable `Opaque` arg (arity preserved, never silently dropped).
[[nodiscard]] CoreCallArg lower_call_arg(const ExprRef &arg) {
    CoreCallArg out;
    if (arg.ptr == nullptr) {
        out.kind = CoreCallArg::Kind::Opaque;
        out.text = "<null-arg>";
        return out;
    }
    std::visit(Overloaded{
                   [&](const BoolLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.value ? "true" : "false";
                   },
                   [&](const IntegerLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.spelling;
                   },
                   [&](const FloatLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.spelling;
                   },
                   [&](const DecimalLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.spelling;
                   },
                   [&](const StringLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.spelling;
                   },
                   [&](const DurationLiteralExpr &e) {
                       out.kind = CoreCallArg::Kind::Literal;
                       out.text = e.spelling;
                   },
                   [&](const PathExpr &e) {
                       out.kind = CoreCallArg::Kind::Path;
                       out.text = path_text(e.path);
                   },
                   [&](const QualifiedValueExpr &e) {
                       out.kind = CoreCallArg::Kind::Qualified;
                       out.text = e.value;
                   },
                   // Bounded exemption: any richer argument shape (nested call,
                   // arithmetic, struct literal, match, …) is recorded opaquely
                   // with a reason. Full argument lowering / value layout is a
                   // later KR6.4 sub-slice; arity is still preserved here.
                   [&](const auto &) {
                       out.kind = CoreCallArg::Kind::Opaque;
                       out.text = "<unlowered-expr>";
                   },
               },
               arg.ptr->node);
    return out;
}

/// If `expr` is a capability call, append its lowered call site to `out`.
/// A capability call is a `CallExpr` whose resolved `callee_ref` is a
/// capability symbol. Nested capability calls inside its arguments are NOT
/// recursed into here (arguments are lowered opaquely in this slice); the
/// executor sees one call site per source capability invocation statement.
void collect_call_from_expr(const ExprRef &expr,
                            const CapabilityEffectIndex &effects,
                            std::vector<CoreCapabilityCall> &out) {
    if (expr.ptr == nullptr) {
        return;
    }
    const auto *call = std::get_if<CallExpr>(&expr.ptr->node);
    if (call == nullptr) {
        return;
    }
    if (call->callee_ref.kind != SymbolRefKind::Capability) {
        return;
    }
    CoreCapabilityCall lowered;
    lowered.callee_name = call->callee;
    lowered.callee_ref = call->callee_ref;
    lowered.effect_kind = effects.lookup(call->callee_ref);
    lowered.args.reserve(call->arguments.size());
    for (const ExprRef &arg : call->arguments) {
        lowered.args.push_back(lower_call_arg(arg));
    }
    out.push_back(std::move(lowered));
}

// Forward declaration: block walking recurses through nested blocks.
void collect_calls_from_block(const Block &block,
                              const CapabilityEffectIndex &effects,
                              std::vector<CoreCapabilityCall> &out);

/// Walk one statement, appending capability call sites in source (pre-order)
/// traversal. Control-flow structure is not modelled yet — but branch bodies
/// are still descended so a capability call inside an `if` is not lost.
void collect_calls_from_statement(const Statement &stmt,
                                  const CapabilityEffectIndex &effects,
                                  std::vector<CoreCapabilityCall> &out) {
    std::visit(Overloaded{
                   [&](const LetStatement &s) {
                       collect_call_from_expr(s.initializer, effects, out);
                   },
                   [&](const AssignStatement &s) {
                       collect_call_from_expr(s.value, effects, out);
                   },
                   [&](const ExprStatement &s) {
                       collect_call_from_expr(s.expr, effects, out);
                   },
                   [&](const ReturnStatement &s) {
                       collect_call_from_expr(s.value, effects, out);
                   },
                   [&](const IfStatement &s) {
                       // Condition first, then branch bodies (pre-order).
                       collect_call_from_expr(s.condition, effects, out);
                       if (s.then_block) {
                           collect_calls_from_block(*s.then_block, effects, out);
                       }
                       if (s.else_block) {
                           collect_calls_from_block(*s.else_block, effects, out);
                       }
                   },
                   [&](const IfLetStatement &s) {
                       collect_call_from_expr(s.scrutinee, effects, out);
                       if (s.then_block) {
                           collect_calls_from_block(*s.then_block, effects, out);
                       }
                       if (s.else_block) {
                           collect_calls_from_block(*s.else_block, effects, out);
                       }
                   },
                   // Statements that cannot host a capability call in this slice
                   // (goto / assert / requires / unwrap / unreachable): nothing
                   // to collect. Their operands are Bool/Option predicates, not
                   // capability invocations.
                   [](const auto &) {},
               },
               stmt.node);
}

void collect_calls_from_block(const Block &block,
                              const CapabilityEffectIndex &effects,
                              std::vector<CoreCapabilityCall> &out) {
    for (const StatementPtr &stmt : block.statements) {
        if (stmt) {
            collect_calls_from_statement(*stmt, effects, out);
        }
    }
}

/// Lower one `ir::FlowDecl` into a `CoreFlowDecl`: per-state capability call
/// sequences extracted from the handler bodies, in source handler order.
[[nodiscard]] CoreFlowDecl lower_flow(const FlowDecl &flow,
                                      const CapabilityEffectIndex &effects) {
    CoreFlowDecl out;
    out.target_ref = flow.target_ref;
    out.agent_name = flow.target_ref.local_name.empty()
                         ? flow.target_ref.canonical_name
                         : flow.target_ref.local_name;
    out.states.reserve(flow.state_handlers.size());
    for (const StateHandler &handler : flow.state_handlers) {
        CoreFlowState state;
        state.state_name = handler.state_name;
        collect_calls_from_block(handler.body, effects, state.calls);
        out.states.push_back(std::move(state));
    }
    return out;
}

} // namespace

CoreProgram lower_ahfl_to_core(const AhflIr &ahfl_ir) {
    CoreProgram core;
    // Build the capability effect-kind index once so each flow call site can
    // carry its effect category without a per-call scan.
    const CapabilityEffectIndex effects = build_effect_index(ahfl_ir);
    // Visit declarations in source order and emit in that order: deterministic.
    for (const Decl &decl : ahfl_ir.declarations) {
        std::visit(Overloaded{
                       [&](const AgentDecl &agent) {
                           core.declarations.emplace_back(lower_agent(agent));
                       },
                       [&](const CapabilityDecl &cap) {
                           core.declarations.emplace_back(lower_capability(cap));
                       },
                       [&](const FlowDecl &flow) {
                           core.declarations.emplace_back(lower_flow(flow, effects));
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

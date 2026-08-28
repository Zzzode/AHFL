// ---------------------------------------------------------------------------
// AhflIr -> Core-IR lowering (RFC 0026 P3, KR6.4) — A-normal form
// ---------------------------------------------------------------------------
//
// See `include/ahfl/compiler/ir/core_ir.hpp` for the layer contract. This pass
// lowers agent state machines, capability imports, and flow handler bodies into
// the execution layer. Handler bodies are A-normalized (Rust MIR / Swift SIL
// style): pure computation lands in a per-flow `CoreExpr` arena and EVERY
// capability invocation is an ordered `CoreCapabilityCallStmt`, so eval order,
// data dependency, pending/suspend, resume checkpoints, and no-replay are
// structural facts. Identity is index-based (Principle 2): CoreValueId /
// CoreExprId / CoreCapabilityId / CoreStateId / field & variant indices. The
// pass is fail-closed: unresolved callees and effectful-unsupported shapes emit
// a structured diagnostic (never a throw, a silent drop, or an Unknown node).

#include "ahfl/compiler/ir/core_ir.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/mangling.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

// Out-of-line structural equality for the recursive body types (used by tests).
bool operator==(const CoreIfStmt &a, const CoreIfStmt &b) noexcept {
    if (!(a.condition == b.condition)) {
        return false;
    }
    const auto region_eq = [](const std::unique_ptr<CoreRegion> &x,
                              const std::unique_ptr<CoreRegion> &y) {
        if (!x || !y) {
            return x.get() == y.get();
        }
        return *x == *y;
    };
    return region_eq(a.then_region, b.then_region) &&
           region_eq(a.else_region, b.else_region);
}

bool operator==(const CoreStmt &a, const CoreStmt &b) noexcept {
    return a.node == b.node && a.source_range == b.source_range;
}

bool operator==(const CoreRegion &a, const CoreRegion &b) noexcept {
    return a.statements == b.statements;
}

bool operator==(const CoreFlowState &a, const CoreFlowState &b) noexcept {
    return a.state == b.state && a.state_name == b.state_name &&
           a.policy == b.policy && a.body == b.body;
}

namespace {

[[nodiscard]] bool symbol_ref_equal(const SymbolRef &a, const SymbolRef &b) {
    return a.kind == b.kind && a.canonical_name == b.canonical_name &&
           a.local_name == b.local_name && a.module_name == b.module_name &&
           a.id == b.id;
}

} // namespace

bool operator==(const CoreFlowDecl &a, const CoreFlowDecl &b) noexcept {
    return a.agent_name == b.agent_name && symbol_ref_equal(a.target_ref, b.target_ref) &&
           a.exprs == b.exprs && a.value_count == b.value_count && a.states == b.states;
}

namespace {

// ---------------------------------------------------------------------------
// TypeRef clone (TypeRef owns children via Owned<TypeRef>, move-only).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Type environment: name -> typed field / variant indices (Principle 2).
//
// P3 resolves names to typed indices so P4 (physical layout) never re-queries
// AHFL-IR / the type environment. Struct fields and enum variants are keyed by
// DECLARATION-ORDER index. Well-known stdlib enums (Option/Result), whose
// EnumDecl lives in the sysroot and may not be inlined in the user program, get
// a builtin variant table. The builtin ORDER MUST MATCH the stdlib declaration
// order (std/option.ahfl: `Some(T)` then `None`), guarded by a sync test.
// ---------------------------------------------------------------------------

struct StructInfo {
    std::vector<std::string> field_names; // index == CoreFieldId
};
struct EnumInfo {
    std::vector<std::string> variant_names; // index == CoreVariantId
};

/// Builtin variant order for well-known stdlib enums. Kept in one place so the
/// sync test can assert it matches the sysroot declaration order.
[[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>> &
builtin_enum_variants() {
    static const std::unordered_map<std::string, std::vector<std::string>> table = {
        // std/option.ahfl declares `Some(T)` before `None`.
        {"Option", {"Some", "None"}},
        // std/result.ahfl declares `Ok(T)` before `Err(E)`.
        {"Result", {"Ok", "Err"}},
    };
    return table;
}

class TypeEnv {
  public:
    void add_struct(const StructDecl &decl) {
        StructInfo info;
        info.field_names.reserve(decl.fields.size());
        for (const FieldDecl &f : decl.fields) {
            info.field_names.push_back(f.name);
        }
        structs_.emplace(decl.name, std::move(info));
        // Also index by last path segment (unqualified) for convenience.
        structs_.emplace(unqualified(decl.name), structs_.at(decl.name));
    }
    void add_enum(const EnumDecl &decl) {
        EnumInfo info;
        info.variant_names.reserve(decl.variants.size());
        for (const EnumVariantDecl &v : decl.variants) {
            info.variant_names.push_back(v.name);
        }
        enums_.emplace(decl.name, std::move(info));
        enums_.emplace(unqualified(decl.name), enums_.at(decl.name));
    }

    /// Resolve a struct field name to its declaration-order index.
    [[nodiscard]] std::optional<std::uint32_t> field_index(const std::string &type_name,
                                                           const std::string &field) const {
        const auto it = structs_.find(type_name);
        if (it == structs_.end()) {
            return std::nullopt;
        }
        const auto &names = it->second.field_names;
        for (std::uint32_t i = 0; i < names.size(); ++i) {
            if (names[i] == field) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Resolve an enum variant name to its declaration-order index (user enum
    /// first, then the builtin table for well-known stdlib enums).
    [[nodiscard]] std::optional<std::uint32_t> variant_index(const std::string &enum_name,
                                                             const std::string &variant) const {
        const auto lookup = [&variant](const std::vector<std::string> &names)
            -> std::optional<std::uint32_t> {
            for (std::uint32_t i = 0; i < names.size(); ++i) {
                if (names[i] == variant) {
                    return i;
                }
            }
            return std::nullopt;
        };
        if (const auto it = enums_.find(enum_name); it != enums_.end()) {
            if (auto idx = lookup(it->second.variant_names)) {
                return idx;
            }
        }
        const auto &builtin = builtin_enum_variants();
        if (const auto it = builtin.find(unqualified(enum_name)); it != builtin.end()) {
            return lookup(it->second);
        }
        return std::nullopt;
    }

  private:
    [[nodiscard]] static std::string unqualified(const std::string &name) {
        const auto pos = name.rfind("::");
        return pos == std::string::npos ? name : name.substr(pos + 2);
    }
    std::unordered_map<std::string, StructInfo> structs_;
    std::unordered_map<std::string, EnumInfo> enums_;
};

// ---------------------------------------------------------------------------
// Capability index: canonical identity -> CoreCapabilityId + effect kind.
// ---------------------------------------------------------------------------
struct CapabilityInfo {
    CoreCapabilityId id{};
    CapabilityEffectKind effect_kind{CapabilityEffectKind::Unknown};
};

class CapabilityIndex {
  public:
    void add(const SymbolRef &ref, CoreCapabilityId id, CapabilityEffectKind kind) {
        if (ref.id.has_value()) {
            by_id_.emplace(*ref.id, CapabilityInfo{id, kind});
        }
        if (!ref.canonical_name.empty()) {
            by_name_.emplace(ref.canonical_name, CapabilityInfo{id, kind});
        }
    }
    [[nodiscard]] std::optional<CapabilityInfo> lookup(const SymbolRef &ref) const {
        if (ref.id.has_value()) {
            if (const auto it = by_id_.find(*ref.id); it != by_id_.end()) {
                return it->second;
            }
        }
        if (!ref.canonical_name.empty()) {
            if (const auto it = by_name_.find(ref.canonical_name); it != by_name_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

  private:
    std::unordered_map<std::size_t, CapabilityInfo> by_id_;
    std::unordered_map<std::string, CapabilityInfo> by_name_;
};

// ---------------------------------------------------------------------------
// Agent state index: state name -> CoreStateId, for goto resolution.
// ---------------------------------------------------------------------------
class StateIndex {
  public:
    explicit StateIndex(const std::vector<std::string> &states) {
        for (std::uint32_t i = 0; i < states.size(); ++i) {
            by_name_.emplace(states[i], CoreStateId{i});
        }
    }
    [[nodiscard]] std::optional<CoreStateId> lookup(const std::string &name) const {
        if (const auto it = by_name_.find(name); it != by_name_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

  private:
    std::unordered_map<std::string, CoreStateId> by_name_;
};

// ---------------------------------------------------------------------------
// Agent state-machine lowering (unchanged from the prior increment).
// ---------------------------------------------------------------------------
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

[[nodiscard]] CoreAgentDecl lower_agent(const AgentDecl &agent) {
    CoreAgentDecl out;
    out.name = agent.name;
    out.symbol_ref = agent.symbol_ref;

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

    std::unordered_map<std::string, std::uint32_t> index_of;
    out.states.reserve(agent.states.size());
    for (const std::string &state : agent.states) {
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
    return out;
}

[[nodiscard]] CoreCapabilityDecl lower_capability(const CapabilityDecl &cap) {
    CoreCapabilityDecl out;
    out.name = cap.name;
    out.symbol_ref = cap.symbol_ref;
    out.effect_kind = cap.effect.kind;
    out.param_types.reserve(cap.params.size());
    for (const ParamDecl &param : cap.params) {
        out.param_types.push_back(clone_type_ref(param.type_ref));
    }
    out.return_type_ref = clone_type_ref(cap.return_type_ref);
    return out;
}

// ---------------------------------------------------------------------------
// Flow handler body lowering (A-normal form).
// ---------------------------------------------------------------------------

/// Maps a binary op from AHFL-IR to Core-IR. Returns nullopt for ops that are
/// not part of the pure computation set this slice lowers.
[[nodiscard]] std::optional<CoreBinaryOp> map_binary_op(ExprBinaryOp op) {
    switch (op) {
    case ExprBinaryOp::Add: return CoreBinaryOp::Add;
    case ExprBinaryOp::Subtract: return CoreBinaryOp::Sub;
    case ExprBinaryOp::Multiply: return CoreBinaryOp::Mul;
    case ExprBinaryOp::Divide: return CoreBinaryOp::Div;
    case ExprBinaryOp::Modulo: return CoreBinaryOp::Mod;
    case ExprBinaryOp::Equal: return CoreBinaryOp::Eq;
    case ExprBinaryOp::NotEqual: return CoreBinaryOp::Ne;
    case ExprBinaryOp::Less: return CoreBinaryOp::Lt;
    case ExprBinaryOp::LessEqual: return CoreBinaryOp::Le;
    case ExprBinaryOp::Greater: return CoreBinaryOp::Gt;
    case ExprBinaryOp::GreaterEqual: return CoreBinaryOp::Ge;
    case ExprBinaryOp::And: return CoreBinaryOp::And;
    case ExprBinaryOp::Or: return CoreBinaryOp::Or;
    // `Implies` has no direct execution-layer op in this slice.
    default: return std::nullopt;
    }
}

[[nodiscard]] CorePathRoot map_path_root(PathRootKind kind, const std::string &root_name) {
    switch (kind) {
    case PathRootKind::Input: return CorePathRoot::Input;
    case PathRootKind::Context: return CorePathRoot::Context;
    default:
        // An identifier root may name a local binding; the lowerer decides.
        (void)root_name;
        return CorePathRoot::Identifier;
    }
}

/// Human-readable source expression kind for unsupported-node diagnostics.
[[nodiscard]] std::string expr_kind_name(const ExprNode &node) {
    return std::visit(Overloaded{
                          [](const MethodCallExpr &) { return std::string("MethodCallExpr"); },
                          [](const LambdaExpr &) { return std::string("LambdaExpr"); },
                          [](const MemberAccessExpr &) { return std::string("MemberAccessExpr"); },
                          [](const IndexAccessExpr &) { return std::string("IndexAccessExpr"); },
                          [](const MatchExpr &) { return std::string("MatchExpr"); },
                          [](const UnwrapExpr &) { return std::string("UnwrapExpr"); },
                          [](const QuantifierExpr &) { return std::string("QuantifierExpr"); },
                          [](const CallExpr &) { return std::string("CallExpr"); },
                          [](const auto &) { return std::string("Expr"); },
                      },
                      node);
}

/// Lowers one flow's handler bodies into ANF. Owns the per-flow expr arena,
/// value counter, and (per-state) local scope. Accumulates diagnostics.
class FlowLowerer {
  public:
    FlowLowerer(CoreFlowDecl &flow, const CapabilityIndex &caps, const StateIndex &states,
                const TypeEnv &types, std::vector<CoreLowerDiagnostic> &diags)
        : flow_(flow), caps_(caps), states_(states), types_(types), diags_(diags) {}

    void lower_handler(const StateHandler &handler, CoreStateId state_id) {
        CoreFlowState core_state;
        core_state.state = state_id;
        core_state.state_name = handler.state_name;
        core_state.policy = lower_policy(handler.policy);
        locals_.clear();
        core_state.body = lower_block(handler.body);
        flow_.states.push_back(std::move(core_state));
    }

  private:
    // --- allocation helpers ---
    [[nodiscard]] CoreValueId fresh_value() {
        return CoreValueId{flow_.value_count++};
    }
    [[nodiscard]] CoreExprId push_expr(CoreExprNode node, SourceRangeOpt range) {
        const auto idx = static_cast<std::uint32_t>(flow_.exprs.size());
        flow_.exprs.push_back(CoreExpr{std::move(node), std::move(range)});
        return CoreExprId{idx};
    }
    void error(std::string code, std::string message, SourceRangeOpt range) {
        diags_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error, std::move(code),
                                             std::move(message), std::move(range)});
    }

    [[nodiscard]] CoreStatePolicy lower_policy(const std::vector<StatePolicyItem> &policy) {
        CoreStatePolicy out;
        for (const StatePolicyItem &item : policy) {
            std::visit(Overloaded{
                           [&](const RetryPolicy &r) { out.retry_limit = r.limit; },
                           [&](const RetryOnPolicy &r) { out.retry_on = r.targets; },
                           [&](const TimeoutPolicy &t) { out.timeout = t.duration; },
                       },
                       item);
        }
        return out;
    }

    // --- region / statement lowering ---
    [[nodiscard]] CoreRegion lower_block(const Block &block) {
        CoreRegion region;
        for (const StatementPtr &stmt : block.statements) {
            if (stmt) {
                lower_statement(*stmt, region);
            }
        }
        return region;
    }

    void lower_statement(const Statement &stmt, CoreRegion &region) {
        std::visit(Overloaded{
                       [&](const LetStatement &s) { lower_let(s, region); },
                       [&](const AssignStatement &s) { lower_assign(s, stmt.source_range, region); },
                       [&](const ExprStatement &s) {
                           // An expression statement is evaluated for its effect;
                           // the produced value id (if any) is discarded.
                           static_cast<void>(lower_value(s.expr, region));
                       },
                       [&](const IfStatement &s) { lower_if(s, stmt.source_range, region); },
                       [&](const GotoStatement &s) { lower_goto(s, stmt.source_range, region); },
                       [&](const ReturnStatement &s) { lower_return(s, stmt.source_range, region); },
                       // Statements without an execution-layer form in this slice
                       // (if-let / assert / requires / unwrap / unreachable) are
                       // deferred; a diagnostic keeps the omission observable but
                       // is a WARNING (they carry no capability effect to lose).
                       [&](const auto &) {
                           diags_.push_back(CoreLowerDiagnostic{
                               CoreDiagnosticSeverity::Warning, "core.UNLOWERED_STATEMENT",
                               "statement kind not yet lowered to Core-IR (deferred sub-slice)",
                               stmt.source_range});
                       },
                   },
                   stmt.node);
    }

    void lower_let(const LetStatement &s, CoreRegion &region) {
        const CoreValueId value = lower_value(s.initializer, region);
        // Track the local's binding so later path reads resolve to this value id.
        locals_[s.name] = value;
    }

    void lower_assign(const AssignStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId value = lower_value(s.value, region);
        CorePlace place;
        place.root = map_path_root(s.target.root_kind, s.target.root_name);
        place.root_name = s.target.root_name;
        place.members = s.target.members;
        region.statements.push_back(
            CoreStmt{CoreStoreStmt{std::move(place), value}, std::move(range)});
    }

    void lower_if(const IfStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId cond = lower_value(s.condition, region);
        CoreIfStmt node;
        node.condition = cond;
        // Each branch is its own region: mutual exclusion is preserved (the two
        // branches are NOT appended to one flat list).
        if (s.then_block) {
            node.then_region = std::make_unique<CoreRegion>(lower_block(*s.then_block));
        } else {
            node.then_region = std::make_unique<CoreRegion>();
        }
        if (s.else_block) {
            node.else_region = std::make_unique<CoreRegion>(lower_block(*s.else_block));
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    void lower_goto(const GotoStatement &s, SourceRangeOpt range, CoreRegion &region) {
        CoreGotoStmt node;
        node.target_name = s.target_state;
        if (const auto id = states_.lookup(s.target_state)) {
            node.target = *id;
        } else {
            error("core.UNKNOWN_GOTO_TARGET",
                  "goto targets unknown state '" + s.target_state + "'", range);
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    void lower_return(const ReturnStatement &s, SourceRangeOpt range, CoreRegion &region) {
        CoreReturnStmt node;
        if (s.value.ptr != nullptr) {
            node.has_value = true;
            node.value = lower_value(s.value, region);
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    // --- value (ANF) lowering ---
    //
    // Returns the CoreValueId holding the expression's result. Capability calls
    // are hoisted to a CoreCapabilityCallStmt appended to `region` (in
    // left-to-right eval order); pure expressions are bound via a CoreLetStmt.
    [[nodiscard]] CoreValueId lower_value(const ExprRef &expr, CoreRegion &region) {
        if (expr.ptr == nullptr) {
            error("core.NULL_EXPR", "null expression in flow body", std::nullopt);
            return fresh_value();
        }
        const SourceRangeOpt range = expr.ptr->source_range;
        return std::visit(
            Overloaded{
                [&](const CallExpr &call) { return lower_call_value(call, expr, range, region); },
                [&](const BoolLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Bool, e.value ? "true" : "false"},
                                     range, region);
                },
                [&](const IntegerLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Integer, e.spelling}, range, region);
                },
                [&](const FloatLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Float, e.spelling}, range, region);
                },
                [&](const DecimalLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Decimal, e.spelling}, range, region);
                },
                [&](const StringLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::String, e.spelling}, range, region);
                },
                [&](const DurationLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Duration, e.spelling}, range, region);
                },
                [&](const UnitLiteralExpr &) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Unit, ""}, range, region);
                },
                [&](const PathExpr &e) { return lower_path_value(e, range, region); },
                [&](const QualifiedValueExpr &e) {
                    return bind_pure(CoreQualifiedExpr{e.value}, range, region);
                },
                [&](const UnaryExpr &e) { return lower_unary_value(e, range, region); },
                [&](const BinaryExpr &e) { return lower_binary_value(e, range, region); },
                [&](const StructLiteralExpr &e) { return lower_struct_value(e, range, region); },
                // Effectful-or-complex shapes not yet lowered. CRITICAL: if the
                // subtree contains a capability call we MUST fail closed (never
                // hide an effect); otherwise a pure unsupported node is recorded
                // with its enumerated kind + range for the verifier to reject.
                // Routed through the ExprRef so the capability check can recurse.
                [&](const auto &node) {
                    static_cast<void>(node);
                    return lower_unsupported_value(expr, expr_kind_name(expr.ptr->node), range,
                                                   region);
                },
            },
            expr.ptr->node);
    }

    [[nodiscard]] CoreValueId bind_pure(CoreExprNode node, SourceRangeOpt range, CoreRegion &region) {
        const CoreExprId expr_id = push_expr(std::move(node), range);
        const CoreValueId value = fresh_value();
        region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
        return value;
    }

    [[nodiscard]] CoreValueId lower_path_value(const PathExpr &e, SourceRangeOpt range,
                                               CoreRegion &region) {
        CorePathExpr node;
        node.root = map_path_root(e.path.root_kind, e.path.root_name);
        node.root_name = e.path.root_name;
        node.members = e.path.members;
        // Resolve an identifier root that names an in-scope local to its value id.
        if (node.root == CorePathRoot::Identifier && e.path.members.empty()) {
            if (const auto it = locals_.find(e.path.root_name); it != locals_.end()) {
                // A bare local reference lowers directly to its bound value id.
                return it->second;
            }
        }
        if (node.root == CorePathRoot::Identifier) {
            if (const auto it = locals_.find(e.path.root_name); it != locals_.end()) {
                node.root = CorePathRoot::Local;
                node.local = it->second;
                node.has_local = true;
            }
        }
        return bind_pure(std::move(node), range, region);
    }

    [[nodiscard]] CoreValueId lower_unary_value(const UnaryExpr &e, SourceRangeOpt range,
                                                CoreRegion &region) {
        // Operand lowered first; if it is effectful its call stmt is hoisted.
        const CoreValueId operand_val = lower_value(e.operand, region);
        const CoreExprId operand_ref = push_expr(CoreValueRefExpr{operand_val},
                                                 e.operand.ptr ? e.operand.ptr->source_range
                                                               : std::nullopt);
        const CoreUnaryOp op = (e.op == ExprUnaryOp::Negate) ? CoreUnaryOp::Neg : CoreUnaryOp::Not;
        return bind_pure(CoreUnaryExpr{op, operand_ref}, range, region);
    }

    [[nodiscard]] CoreValueId lower_binary_value(const BinaryExpr &e, SourceRangeOpt range,
                                                 CoreRegion &region) {
        const auto op = map_binary_op(e.op);
        if (!op.has_value()) {
            // Only `Implies` reaches here. Fail closed if either operand carries
            // an effect; otherwise record a pure unsupported node.
            if (expr_has_capability_call(e.lhs) || expr_has_capability_call(e.rhs)) {
                error("core.EFFECTFUL_UNSUPPORTED",
                      "binary operator carries a capability effect but is not yet "
                      "lowered to Core-IR",
                      range);
                return fresh_value();
            }
            const CoreExprId expr_id = push_expr(CoreUnsupportedExpr{"BinaryExpr", range}, range);
            const CoreValueId value = fresh_value();
            region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
            return value;
        }
        // Left-to-right: lhs fully lowered (effects hoisted) before rhs.
        const CoreValueId lhs_val = lower_value(e.lhs, region);
        const CoreValueId rhs_val = lower_value(e.rhs, region);
        const CoreExprId lhs_ref =
            push_expr(CoreValueRefExpr{lhs_val}, e.lhs.ptr ? e.lhs.ptr->source_range : std::nullopt);
        const CoreExprId rhs_ref =
            push_expr(CoreValueRefExpr{rhs_val}, e.rhs.ptr ? e.rhs.ptr->source_range : std::nullopt);
        return bind_pure(CoreBinaryExpr{*op, lhs_ref, rhs_ref}, range, region);
    }

    [[nodiscard]] CoreValueId lower_struct_value(const StructLiteralExpr &e, SourceRangeOpt range,
                                                 CoreRegion &region) {
        // A struct / enum-variant constructor is PURE. Its field/payload values
        // are lowered first (effects hoisted, left-to-right), then the construct
        // consumes their value ids.
        CoreConstructExpr node;
        node.type_name = e.is_enum_variant ? e.enum_name : e.type_name;
        node.is_enum_variant = e.is_enum_variant;
        node.variant_name = e.variant_name;
        // Resolve the enum variant to its typed declaration-order index
        // (Principle 2). An unresolvable user variant is fail-closed; the
        // string names remain for display only.
        if (e.is_enum_variant) {
            if (const auto idx = types_.variant_index(e.enum_name, e.variant_name)) {
                node.variant = *idx;
                node.variant_resolved = true;
            } else {
                error("core.UNRESOLVED_ENUM_VARIANT",
                      "enum variant '" + e.enum_name + "::" + e.variant_name +
                          "' could not be resolved to a declared variant index",
                      range);
            }
        }
        node.args.reserve(e.fields.size());
        for (const StructFieldInit &field : e.fields) {
            node.args.push_back(lower_value(field.value, region));
        }
        return bind_pure(std::move(node), range, region);
    }

    /// The A-normalization core: a capability call becomes an ordered statement.
    [[nodiscard]] CoreValueId lower_call_value(const CallExpr &call, const ExprRef &expr,
                                               SourceRangeOpt range, CoreRegion &region) {
        if (call.callee_ref.kind == SymbolRefKind::Capability) {
            const auto info = caps_.lookup(call.callee_ref);
            if (!info.has_value()) {
                // Fail closed: an unresolved capability call must not produce an
                // Unknown node the backend would treat as a no-op.
                error("core.UNRESOLVED_CAPABILITY_CALL",
                      "capability call '" + call.callee +
                          "' could not be resolved to a capability declaration",
                      range);
                return fresh_value();
            }
            CoreCapabilityCallStmt stmt;
            stmt.result = fresh_value();
            stmt.capability = info->id;
            stmt.callee_name = call.callee;
            stmt.args.reserve(call.arguments.size());
            // Arguments lowered left-to-right; a nested capability call among
            // them is itself hoisted to a preceding statement (recursion).
            for (const ExprRef &arg : call.arguments) {
                stmt.args.push_back(lower_value(arg, region));
            }
            const CoreValueId result = stmt.result;
            region.statements.push_back(CoreStmt{std::move(stmt), range});
            return result;
        }
        // A non-capability call may be an ENUM-VARIANT CONSTRUCTOR — the front
        // end lowers `Enum::Variant(payload)` to a CallExpr whose callee is the
        // qualified variant path (e.g. "Box::Wrap", "std::option::Option::Some").
        // These are PURE constructors: their arguments are lowered first (a
        // nested capability call among them is hoisted, A-normal), then a
        // CoreConstructExpr consumes the resulting value ids.
        if (auto ev = split_enum_variant(call.callee)) {
            if (const auto idx = types_.variant_index(ev->first, ev->second)) {
                CoreConstructExpr node;
                node.type_name = ev->first;
                node.variant_name = ev->second;
                node.is_enum_variant = true;
                node.variant = *idx;
                node.variant_resolved = true;
                node.args.reserve(call.arguments.size());
                for (const ExprRef &arg : call.arguments) {
                    node.args.push_back(lower_value(arg, region));
                }
                return bind_pure(std::move(node), range, region);
            }
        }
        // Otherwise: a free-function call (not modelled by this slice) or an
        // unresolved variant. Fail closed if the subtree carries an effect;
        // else record a pure unsupported node.
        return lower_unsupported_value(expr, "CallExpr", range, region);
    }

    /// Split a qualified variant-constructor callee ("a::b::Enum::Variant") into
    /// its enum name and variant name. Returns nullopt if it has no `::`.
    [[nodiscard]] static std::optional<std::pair<std::string, std::string>>
    split_enum_variant(const std::string &callee) {
        const auto last = callee.rfind("::");
        if (last == std::string::npos) {
            return std::nullopt;
        }
        const std::string variant = callee.substr(last + 2);
        std::string enum_name = callee.substr(0, last);
        // Reduce the enum path to its last segment (the TypeEnv also indexes the
        // unqualified name); e.g. "std::option::Option" -> "Option".
        const auto enum_last = enum_name.rfind("::");
        if (enum_last != std::string::npos) {
            enum_name = enum_name.substr(enum_last + 2);
        }
        if (enum_name.empty() || variant.empty()) {
            return std::nullopt;
        }
        return std::make_pair(enum_name, variant);
    }

    /// Fail-closed handler for a not-yet-lowered expression. If the subtree
    /// contains ANY capability call, this is an ERROR (we must never silently
    /// drop an effect). Otherwise it is a pure `CoreUnsupportedExpr` (enumerated
    /// kind + range) that the Core-IR verifier can reject at backend time.
    [[nodiscard]] CoreValueId lower_unsupported_value(const ExprRef &expr, std::string kind,
                                                      SourceRangeOpt range, CoreRegion &region) {
        if (expr_has_capability_call(expr)) {
            error("core.EFFECTFUL_UNSUPPORTED",
                  "expression kind '" + kind +
                      "' carries a capability effect but is not yet lowered to Core-IR "
                      "(cannot be reduced to A-normal form in this slice)",
                  range);
            return fresh_value();
        }
        const CoreExprId expr_id = push_expr(CoreUnsupportedExpr{std::move(kind), range}, range);
        const CoreValueId value = fresh_value();
        region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
        return value;
    }

    // --- capability-effect detection over an arbitrary subtree ---
    [[nodiscard]] bool expr_has_capability_call(const ExprRef &expr) const {
        if (expr.ptr == nullptr) {
            return false;
        }
        return std::visit(
            Overloaded{
                [&](const CallExpr &call) {
                    if (call.callee_ref.kind == SymbolRefKind::Capability) {
                        return true;
                    }
                    for (const ExprRef &arg : call.arguments) {
                        if (expr_has_capability_call(arg)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const UnaryExpr &e) { return expr_has_capability_call(e.operand); },
                [&](const BinaryExpr &e) {
                    return expr_has_capability_call(e.lhs) || expr_has_capability_call(e.rhs);
                },
                [&](const MemberAccessExpr &e) { return expr_has_capability_call(e.base); },
                [&](const IndexAccessExpr &e) {
                    return expr_has_capability_call(e.base) || expr_has_capability_call(e.index);
                },
                [&](const UnwrapExpr &e) { return expr_has_capability_call(e.operand); },
                [&](const StructLiteralExpr &e) {
                    for (const StructFieldInit &f : e.fields) {
                        if (expr_has_capability_call(f.value)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const MethodCallExpr &e) {
                    if (expr_has_capability_call(e.receiver)) {
                        return true;
                    }
                    for (const ExprRef &arg : e.arguments) {
                        if (expr_has_capability_call(arg)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const MatchExpr &e) {
                    if (expr_has_capability_call(e.scrutinee)) {
                        return true;
                    }
                    for (const MatchArmExpr &arm : e.arms) {
                        if (expr_has_capability_call(arm.body)) {
                            return true;
                        }
                    }
                    return false;
                },
                [](const auto &) { return false; },
            },
            expr.ptr->node);
    }

    CoreFlowDecl &flow_;
    const CapabilityIndex &caps_;
    const StateIndex &states_;
    const TypeEnv &types_;
    std::vector<CoreLowerDiagnostic> &diags_;
    std::unordered_map<std::string, CoreValueId> locals_;
};

} // namespace

CoreLowerResult lower_ahfl_to_core(const AhflIr &ahfl_ir) {
    CoreLowerResult result;
    CoreProgram &core = result.program;

    // Pass 1: type environment + capability table + agent index.
    TypeEnv types;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *s = std::get_if<StructDecl>(&decl)) {
            types.add_struct(*s);
        } else if (const auto *e = std::get_if<EnumDecl>(&decl)) {
            types.add_enum(*e);
        }
    }

    CapabilityIndex cap_index;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *cap = std::get_if<CapabilityDecl>(&decl)) {
            const auto id = CoreCapabilityId{static_cast<std::uint32_t>(core.capabilities.size())};
            core.capabilities.push_back(lower_capability(*cap));
            cap_index.add(cap->symbol_ref, id, cap->effect.kind);
        }
    }

    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *agent = std::get_if<AgentDecl>(&decl)) {
            core.agents.push_back(lower_agent(*agent));
        }
    }

    // Pass 2: flows. Resolve each flow's target agent to get its state index.
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *flow = std::get_if<FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }
        CoreFlowDecl core_flow;
        core_flow.target_ref = flow->target_ref;
        core_flow.agent_name = flow->target_ref.local_name.empty()
                                   ? flow->target_ref.canonical_name
                                   : flow->target_ref.local_name;

        // Find the target agent's states for goto resolution.
        const CoreAgentDecl *target_agent = nullptr;
        for (const CoreAgentDecl &a : core.agents) {
            if (a.symbol_ref.canonical_name == flow->target_ref.canonical_name &&
                !flow->target_ref.canonical_name.empty()) {
                target_agent = &a;
                break;
            }
        }
        const std::vector<std::string> empty_states;
        StateIndex state_index(target_agent ? target_agent->states : empty_states);

        FlowLowerer lowerer(core_flow, cap_index, state_index, types, result.diagnostics);
        for (const StateHandler &handler : flow->state_handlers) {
            const auto state_id = state_index.lookup(handler.state_name);
            lowerer.lower_handler(handler, state_id.value_or(CoreStateId{0}));
        }
        core.flows.push_back(std::move(core_flow));
    }

    result.is_executable = !result.has_errors();
    return result;
}

} // namespace ahfl::ir::core

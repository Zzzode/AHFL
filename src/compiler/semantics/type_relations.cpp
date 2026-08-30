#include "ahfl/compiler/semantics/type_relations.hpp"

#include "ahfl/compiler/semantics/effect_judgement.hpp"
#include "compiler/semantics/std_container_types.hpp"

#include <functional>
#include <optional>
#include <sstream>
#include <utility>

namespace ahfl {

// ---- Flat trace helpers (declared early; used inside the anonymous ns below)

void TypeRelationContext::record_trace_step(TypeRelationKind kind,
                                            int depth,
                                            std::string path,
                                            std::string expected_describe,
                                            std::string actual_describe,
                                            bool success,
                                            std::string reason) {
    if (!options_.enable_trace) {
        return;
    }
    if (depth >= options_.max_depth) {
        return;
    }
    if (success && !options_.include_success_steps) {
        return;
    }
    TypeRelationTraceStep step;
    step.kind = kind;
    step.depth = depth;
    step.path = std::move(path);
    step.expected_describe = std::move(expected_describe);
    step.actual_describe = std::move(actual_describe);
    step.result = success ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
    step.reason = std::move(reason);
    flat_trace_.steps.push_back(std::move(step));
}

std::vector<std::string> RelationTrace::format_notes(std::size_t max_notes) const {
    std::vector<std::string> notes;
    notes.reserve(std::min(max_notes, steps.size()));

    // First pass: failed steps only.
    for (const auto &s : steps) {
        if (notes.size() >= max_notes)
            break;
        if (s.result == TypeRelationResult::Rejected) {
            std::string note;
            if (!s.path.empty()) {
                note += "at `" + s.path + "`: ";
            }
            note += "expected " + s.expected_describe + ", got " + s.actual_describe;
            if (!s.reason.empty()) {
                note += " (" + s.reason + ")";
            }
            notes.push_back(std::move(note));
        }
    }

    // Second pass: success steps if room.
    for (const auto &s : steps) {
        if (notes.size() >= max_notes)
            break;
        if (s.result == TypeRelationResult::Accepted) {
            std::string note;
            if (!s.path.empty()) {
                note += "at `" + s.path + "`: ";
            }
            note += "ok: " + s.actual_describe + " compatible with " + s.expected_describe;
            notes.push_back(std::move(note));
        }
    }

    return notes;
}

namespace {

// Helper to sync-record a flat trace step whenever a skeleton Relation/Leaf
// node is emitted. Avoids scattering record_trace_step() calls everywhere.
void sync_trace(TypeRelationContext *ctx,
                TypeRelationKind kind,
                const std::string &path,
                const std::string &expected,
                const std::string &actual,
                bool satisfied,
                std::string reason = {}) {
    if (ctx == nullptr || !ctx->options().enable_trace)
        return;
    // Depth approximated by path nesting level (count of '.').
    int depth = 0;
    for (char c : path)
        if (c == '.')
            ++depth;
    ctx->record_trace_step(kind, depth, path, expected, actual, satisfied, std::move(reason));
}

// Append a path segment. If the parent path is empty just return the segment.
std::string join_path(const std::string &parent, std::string_view segment) {
    if (parent.empty()) {
        return std::string(segment);
    }
    if (segment.empty()) {
        return parent;
    }
    return parent + "." + std::string(segment);
}

// Recursive helper used by TypeConstraintSkeleton::to_string.
void dump_node(std::ostringstream &out, const TypeConstraintNode &node, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    out << indent;

    switch (node.kind) {
    case TypeConstraintNode::Kind::And:
        out << "AND";
        break;
    case TypeConstraintNode::Kind::Or:
        out << "OR";
        break;
    case TypeConstraintNode::Kind::Relation:
        out << "RELATION(" << node.relation << ")";
        break;
    case TypeConstraintNode::Kind::Leaf:
        out << "LEAF";
        break;
    }

    out << (node.satisfied ? " [satisfied] " : " [failed] ");

    if (!node.path.empty()) {
        out << "path=\"" << node.path << "\" ";
    }
    if (!node.left_describe.empty() || !node.right_describe.empty()) {
        out << "lhs=\"" << node.left_describe << "\" rhs=\"" << node.right_describe << "\"";
    }
    out << '\n';

    for (const auto &child : node.children) {
        dump_node(out, child, depth + 1);
    }
}

// ------- Skeleton-aware relational traversals ------------------------------
//
// Each `xxx_impl` function carries the active relation name, the current
// path, and an optional reference to the context. When the context is null
// (the default when options().emit_constraint_skeleton is false), the
// skeleton helpers are all no-ops and the overhead is a single
// dereference + conditional per conjunction site.

// RAII guard to push/pop an And/Or node on the context stack. The ctor
// pushes, dtor pops. If `ctx` is null or skeleton emission is disabled,
// this object is completely trivial.
struct FrameGuard {
    TypeRelationContext *ctx;
    bool pushed;

    FrameGuard(TypeRelationContext *c, TypeConstraintNode node) : ctx(c), pushed(false) {
        if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
            ctx->push_node(std::move(node));
            pushed = true;
        }
    }

    FrameGuard(TypeRelationContext *c,
               TypeConstraintNode::Kind kind,
               const std::string &path,
               const Type &lhs,
               const Type &rhs)
        : ctx(c), pushed(false) {
        if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
            TypeConstraintNode node;
            node.kind = kind;
            node.path = path;
            node.left_describe = lhs.describe();
            node.right_describe = rhs.describe();
            ctx->push_node(std::move(node));
            pushed = true;
        }
    }

    ~FrameGuard() {
        if (pushed) {
            ctx->pop_node();
        }
    }

    FrameGuard(const FrameGuard &) = delete;
    FrameGuard &operator=(const FrameGuard &) = delete;
};

// ---- Equivalence -----------------------------------------------------------

bool equivalent_impl(const Type &lhs,
                     const Type &rhs,
                     TypeRelationContext *ctx,
                     const std::string &path,
                     MemoizedRelationSolver &solver);

bool equivalent_leaf(const Type &lhs,
                     const Type &rhs,
                     TypeRelationContext *ctx,
                     const std::string &path,
                     bool value) {
    sync_trace(ctx, TypeRelationKind::Equivalent, path, rhs.describe(), lhs.describe(), value);
    if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::Relation;
        n.relation = "equivalent";
        n.path = path;
        n.left_describe = lhs.describe();
        n.right_describe = rhs.describe();
        n.satisfied = value;
        ctx->push_node(std::move(n));
        ctx->pop_node();
    }
    return value;
}

bool equivalent_pairwise(const Type &lhs_a,
                         const Type &rhs_a,
                         const Type &lhs_b,
                         const Type &rhs_b,
                         const std::string &base,
                         std::string_view seg_a,
                         std::string_view seg_b,
                         MemoizedRelationSolver &solver) {
    const bool ok_a =
        solver.solve(TypeRelationKind::Equivalent, lhs_a, rhs_a, join_path(base, seg_a));
    const bool ok_b =
        solver.solve(TypeRelationKind::Equivalent, lhs_b, rhs_b, join_path(base, seg_b));
    return ok_a && ok_b;
}

[[nodiscard]] bool nominal_name_matches(std::optional<SymbolId> lhs_symbol,
                                        std::string_view lhs_name,
                                        std::optional<SymbolId> rhs_symbol,
                                        std::string_view rhs_name) noexcept {
    if (lhs_symbol.has_value() && rhs_symbol.has_value()) {
        return *lhs_symbol == *rhs_symbol;
    }
    return lhs_name == rhs_name;
}

// RFC 0025: capacity subtyping for bounded collection types. A source
// collection is capacity-assignable to a target when:
//   - the target is unbounded (nullopt)          -> always OK (drop the bound)
//   - both are bounded and source_cap <= target  -> OK (widen the bound)
//   - the target is bounded but the source is not -> FAIL (no static witness)
[[nodiscard]] bool capacity_assignable(std::optional<std::uint64_t> source_capacity,
                                       std::optional<std::uint64_t> target_capacity) noexcept {
    if (!target_capacity.has_value()) {
        return true;
    }
    if (!source_capacity.has_value()) {
        return false;
    }
    return *source_capacity <= *target_capacity;
}

bool equivalent_impl(const Type &lhs,
                     const Type &rhs,
                     TypeRelationContext *ctx,
                     const std::string &path,
                     MemoizedRelationSolver &solver) {
    // Pointer identity short-circuit.
    if (&lhs == &rhs) {
        return equivalent_leaf(lhs, rhs, ctx, path, true);
    }

    if (lhs.payload.index() != rhs.payload.index()) {
        return equivalent_leaf(lhs, rhs, ctx, path, false);
    }

    // Nominal stdlib containers: short-circuit with dedicated path labels
    // before falling through to the generic StructT/EnumT handler. This keeps
    // the constraint-skeleton tree shape aligned with the legacy bridge: only
    // the container's shape/element sub-constraints appear under the outer
    // And (no struct.name / enum.name leaf emitted when the canonical names
    // already agree via the nominal guard).
    const auto lhs_view = stdlib_bridge::std_container_type_view(lhs);
    if (lhs_view.has_value() && lhs_view->nominal && lhs_view->first != nullptr) {
        const auto rhs_view = stdlib_bridge::std_container_type_view(rhs);
        if (rhs_view.has_value() && rhs_view->nominal && rhs_view->kind == lhs_view->kind &&
            rhs_view->first != nullptr) {
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);
            // RFC 0025: bounded collection capacity is part of structural
            // identity, so two collections are equivalent only if their
            // capacities match. The std_container_type_view does not carry the
            // capacity, so read it directly off the StructT payload.
            const auto *lhs_struct = lhs.get_if<types::StructT>();
            const auto *rhs_struct = rhs.get_if<types::StructT>();
            std::optional<std::uint64_t> lhs_capacity;
            std::optional<std::uint64_t> rhs_capacity;
            if (lhs_struct != nullptr) {
                lhs_capacity = lhs_struct->capacity;
            }
            if (rhs_struct != nullptr) {
                rhs_capacity = rhs_struct->capacity;
            }
            if (lhs_capacity != rhs_capacity) {
                return equivalent_leaf(lhs, rhs, ctx, join_path(path, "collection.capacity"), false);
            }
            switch (lhs_view->kind) {
            case stdlib_bridge::StdContainerKind::Option:
                return solver.solve(TypeRelationKind::Equivalent,
                                    *lhs_view->first,
                                    *rhs_view->first,
                                    join_path(path, "optional.inner"));
            case stdlib_bridge::StdContainerKind::List:
                return solver.solve(TypeRelationKind::Equivalent,
                                    *lhs_view->first,
                                    *rhs_view->first,
                                    join_path(path, "list.element"));
            case stdlib_bridge::StdContainerKind::Set:
                return solver.solve(TypeRelationKind::Equivalent,
                                    *lhs_view->first,
                                    *rhs_view->first,
                                    join_path(path, "set.element"));
            case stdlib_bridge::StdContainerKind::Map:
                if (lhs_view->second == nullptr || rhs_view->second == nullptr) {
                    return false;
                }
                return equivalent_pairwise(*lhs_view->first,
                                           *rhs_view->first,
                                           *lhs_view->second,
                                           *rhs_view->second,
                                           path,
                                           "map.key",
                                           "map.value",
                                           solver);
            }
        }
    }

    return lhs.visit(types::Overloads{
        [&](const types::AnyT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::NeverT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::ErrorT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::UnitT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::BoolT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::IntT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::BoundedIntT &l) {
            const auto *r = rhs.get_if<types::BoundedIntT>();
            bool eq = r != nullptr && l.minimum == r->minimum && l.maximum == r->maximum;
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const types::FloatT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::StringT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::UUIDT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::TimestampT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::DurationT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::BoundedStringT &l) {
            const auto *r = rhs.get_if<types::BoundedStringT>();
            bool eq = r != nullptr && l.minimum == r->minimum && l.maximum == r->maximum;
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const types::DecimalT &l) {
            const auto *r = rhs.get_if<types::DecimalT>();
            bool eq = r != nullptr && l.scale == r->scale;
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const types::StructT &l) {
            // Struct equivalence is nominal:
            //   AND ( name match, type_args[i] equivalent for all i )
            const auto *r = rhs.get_if<types::StructT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }

            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);

            const bool name_ok =
                nominal_name_matches(l.symbol, l.canonical_name, r->symbol, r->canonical_name);
            if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
                TypeConstraintNode n;
                n.kind = TypeConstraintNode::Kind::Relation;
                n.relation = "equivalent";
                n.path = join_path(path, "struct.name");
                n.left_describe = lhs.describe();
                n.right_describe = rhs.describe();
                n.satisfied = name_ok;
                ctx->push_node(std::move(n));
                ctx->pop_node();
            }
            if (!name_ok)
                return false;

            // Type arguments must match arity and be pairwise equivalent.
            if (l.type_args.size() != r->type_args.size()) {
                return false;
            }
            for (std::size_t i = 0; i < l.type_args.size(); ++i) {
                if (l.type_args[i] == nullptr || r->type_args[i] == nullptr) {
                    return false;
                }
                if (!solver.solve(TypeRelationKind::Equivalent,
                                  *l.type_args[i],
                                  *r->type_args[i],
                                  join_path(path, "struct.type_args[" + std::to_string(i) + "]"))) {
                    return false;
                }
            }
            // RFC 0025: a bounded collection capacity is part of structural
            // identity. List<Int>(4) and List<Int>(8) are not equivalent, and
            // neither is bounded vs unbounded.
            if (l.capacity != r->capacity) {
                return false;
            }
            return true;
        },
        [&](const types::EnumT &l) {
            const auto *r = rhs.get_if<types::EnumT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);
            const bool name_ok =
                nominal_name_matches(l.symbol, l.canonical_name, r->symbol, r->canonical_name);
            if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
                TypeConstraintNode n;
                n.kind = TypeConstraintNode::Kind::Relation;
                n.relation = "equivalent";
                n.path = join_path(path, "enum.name");
                n.left_describe = lhs.describe();
                n.right_describe = rhs.describe();
                n.satisfied = name_ok;
                ctx->push_node(std::move(n));
                ctx->pop_node();
            }
            if (!name_ok) {
                return false;
            }
            // Type arguments must match arity and be pairwise equivalent.
            if (l.type_args.size() != r->type_args.size()) {
                return false;
            }
            for (std::size_t i = 0; i < l.type_args.size(); ++i) {
                if (l.type_args[i] == nullptr || r->type_args[i] == nullptr) {
                    return false;
                }
                if (!solver.solve(TypeRelationKind::Equivalent,
                                  *l.type_args[i],
                                  *r->type_args[i],
                                  join_path(path, "enum.type_args[" + std::to_string(i) + "]"))) {
                    return false;
                }
            }
            return true;
        },
        [&](const types::EnumVariantT &l) {
            const auto *r = rhs.get_if<types::EnumVariantT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            const bool name_ok =
                l.variant_name == r->variant_name &&
                nominal_name_matches(l.symbol, l.canonical_name, r->symbol, r->canonical_name);
            if (!name_ok) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            // Type arguments must match arity and be pairwise equivalent.
            if (l.type_args.size() != r->type_args.size()) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);
            for (std::size_t i = 0; i < l.type_args.size(); ++i) {
                if (l.type_args[i] == nullptr || r->type_args[i] == nullptr) {
                    return false;
                }
                if (!solver.solve(
                        TypeRelationKind::Equivalent,
                        *l.type_args[i],
                        *r->type_args[i],
                        join_path(path, "variant.type_args[" + std::to_string(i) + "]"))) {
                    return false;
                }
            }
            return true;
        },
        [&](const types::FnT &l) {
            const auto *r = rhs.get_if<types::FnT>();
            if (r == nullptr || l.params.size() != r->params.size()) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);
            // All parameter types must be equivalent.
            for (std::size_t i = 0; i < l.params.size(); ++i) {
                if (l.params[i] == nullptr || r->params[i] == nullptr) {
                    return false;
                }
                if (!solver.solve(TypeRelationKind::Equivalent,
                                  *l.params[i],
                                  *r->params[i],
                                  join_path(path, "fn.param[" + std::to_string(i) + "]"))) {
                    return false;
                }
            }
            // Return type must be equivalent.
            if (l.return_type == nullptr || r->return_type == nullptr) {
                return false;
            }
            if (!solver.solve(TypeRelationKind::Equivalent,
                              *l.return_type,
                              *r->return_type,
                              join_path(path, "fn.return"))) {
                return false;
            }
            // Effect must be equal (structural equality on the judgement).
            return l.effect == r->effect;
        },
        [&](const types::TypeVarT &l) {
            // Type variables are equivalent iff they have the same index within
            // their enclosing generic declaration. Index is the canonical
            // identity (industry standard: position-based substitution keys);
            // the name is diagnostic-only. We additionally require name match
            // as a defensive sanity check.
            //
            // RFC 0013 P2-S1 (R0): scope_id is intentionally NOT part of this
            // equivalence test — relation solving is positional. The global
            // TypeContext interns TypeVars by (index, scope_id, name), so
            // same-scope vars are already pointer-identical; cross-scope vars
            // with the same index are treated as equivalent here, which is the
            // conservative pre-R0 behaviour relation solving relies on.
            const auto *r = rhs.get_if<types::TypeVarT>();
            bool eq = r != nullptr && l.index == r->index && l.name == r->name;
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const auto &) { return equivalent_leaf(lhs, rhs, ctx, path, false); },
    });
}

// ---- Subtype ---------------------------------------------------------------

// KR5.4 (RFC 0013 P5-02): resolve the declared variance of a user nominal's
// type parameter at `index` via the injected variance provider. Defaults to
// Invariant when no provider is set, the nominal is unknown, or the index is
// out of the provided vector's range — exactly reproducing the pre-variance
// behavior. Stdlib containers do not reach this helper (they are handled by
// dedicated branches above).
[[nodiscard]] Variance nominal_arg_variance(const TypeRelationContext *ctx,
                                            std::string_view canonical_name,
                                            std::size_t index) {
    if (ctx == nullptr || !ctx->options().variance_provider) {
        return Variance::Invariant;
    }
    const auto variances = ctx->options().variance_provider(canonical_name);
    if (index >= variances.size()) {
        return Variance::Invariant;
    }
    return variances[index];
}

// Apply the variance rule for one type-argument pair: Covariant defers to
// source <: target, Contravariant to target <: source, Invariant to
// equivalence. Returns whether the pair satisfies the relation.
[[nodiscard]] RelationDecision solve_variant_arg(MemoizedRelationSolver &solver,
                                                 Variance variance,
                                                 const Type &source_arg,
                                                 const Type &target_arg,
                                                 const std::string &path) {
    switch (variance) {
    case Variance::Covariant:
        return solver.solve(TypeRelationKind::Subtype, source_arg, target_arg, path);
    case Variance::Contravariant:
        return solver.solve(TypeRelationKind::Subtype, target_arg, source_arg, path);
    case Variance::Invariant:
        break;
    }
    return solver.solve(TypeRelationKind::Equivalent, source_arg, target_arg, path);
}

RelationDecision subtype_impl(const Type &source,
                              const Type &target,
                              TypeRelationContext *ctx,
                              const std::string &path,
                              MemoizedRelationSolver &solver);

RelationDecision subtype_leaf(const Type &source,
                              const Type &target,
                              TypeRelationContext *ctx,
                              const std::string &path,
                              MemoizedRelationSolver &solver,
                              bool value,
                              std::vector<TypedAdjustmentOp> ops = {}) {
    sync_trace(ctx, TypeRelationKind::Subtype, path, target.describe(), source.describe(), value);
    if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::Relation;
        n.relation = "subtype";
        n.path = path;
        n.left_describe = source.describe();
        n.right_describe = target.describe();
        n.satisfied = value;
        ctx->push_node(std::move(n));
        ctx->pop_node();
    }
    if (!value) {
        return {};
    }
    return solver.accepted_node(source, target, std::move(ops));
}

RelationDecision subtype_impl(const Type &source,
                              const Type &target,
                              TypeRelationContext *ctx,
                              const std::string &path,
                              MemoizedRelationSolver &solver) {
    // Disjunction: subtype can succeed via equivalence, via top/bottom type
    // rules, via structural covariance, or via any of the specific
    // relaxations. Model this as an Or node when the skeleton is enabled.
    FrameGuard real_guard(ctx, TypeConstraintNode::Kind::Or, path, source, target);

    // Pointer identity short-circuit.
    if (&source == &target) {
        return subtype_leaf(source, target, ctx, join_path(path, "identical"), solver, true);
    }

    // Error propagation: Error is both top-and-bottom for error recovery — it is
    // compatible with every type in both directions so a single error doesn't
    // cascade into dozens of spurious secondary diagnostics.
    if (source.holds<types::ErrorT>() || target.holds<types::ErrorT>()) {
        if (solver.produces_witness() && &source != &target) {
            return solver.fail(RelationFailure::UnsupportedAdjustment);
        }
        return subtype_leaf(source, target, ctx, join_path(path, "error"), solver, true);
    }

    // Any is the top type: every type is a subtype of Any.
    if (target.holds<types::AnyT>()) {
        return subtype_leaf(source,
                            target,
                            ctx,
                            join_path(path, "any-top"),
                            solver,
                            true,
                            {TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::ToAny,
                                .arg_index = 0,
                                .child = kInvalidAdjustmentNode,
                            }});
    }

    // Never is the bottom type: Never is a subtype of every type.
    if (source.holds<types::NeverT>()) {
        return subtype_leaf(source,
                            target,
                            ctx,
                            join_path(path, "never-bottom"),
                            solver,
                            true,
                            {TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::FromNever,
                                .arg_index = 0,
                                .child = kInvalidAdjustmentNode,
                            }});
    }

    // Branch 1: equivalence. Relation kind is part of the solver key, so
    // equivalence and subtype cache entries stay separate.
    const RelationDecision eq =
        solver.solve(TypeRelationKind::Equivalent, source, target, join_path(path, "equiv"));
    if (eq) {
        return eq;
    }
    if (eq.failure != RelationFailure::None) {
        return eq;
    }

    // Nominal generic subtyping for struct / enum.
    //
    // For stdlib containers the variance + path-segment contract is hardcoded
    // to match the legacy bridge (covariant element for Option/List/Set; Map
    // keys invariant, Map values covariant). All other user-defined nominals
    // are invariant (type arguments compared via equivalent) until the trait-
    // based variance system ships in PHASE B.
    //
    // See docs/design/corelib-container-migration.zh.md §9.
    // TODO(P5-02): replace hardcoded per-name variance with trait declarations.
    if (source.holds<types::StructT>() && target.holds<types::StructT>()) {
        const auto *s = source.get_if<types::StructT>();
        const auto *t = target.get_if<types::StructT>();
        if (s != nullptr && t != nullptr) {
            if (!nominal_name_matches(s->symbol, s->canonical_name, t->symbol, t->canonical_name)) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "struct.name"), solver, false);
            }
            if (s->type_args.size() != t->type_args.size()) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "struct.arity"), solver, false);
            }
            const auto src_container = stdlib_bridge::std_container_type_view(source);
            if (src_container.has_value() && src_container->nominal &&
                src_container->first != nullptr) {
                const auto tgt_container = stdlib_bridge::std_container_type_view(target);
                if (tgt_container.has_value() && tgt_container->nominal &&
                    tgt_container->kind == src_container->kind && tgt_container->first != nullptr) {
                    // RFC 0025: the source capacity must be assignable to the
                    // target capacity (unbounded target accepts any source;
                    // bounded target requires source_cap <= target_cap and
                    // rejects an unbounded source).
                    if (!capacity_assignable(s->capacity, t->capacity)) {
                        return subtype_leaf(source,
                                            target,
                                            ctx,
                                            join_path(path, "collection.capacity"),
                                            solver,
                                            false);
                    }
                    std::vector<TypedAdjustmentOp> ops;
                    if (s->capacity != t->capacity) {
                        ops.push_back(TypedAdjustmentOp{
                            .kind = TypedAdjustmentOpKind::CapacityWiden,
                            .arg_index = 0,
                            .child = kInvalidAdjustmentNode,
                        });
                    }
                    switch (src_container->kind) {
                    case stdlib_bridge::StdContainerKind::Option:
                    case stdlib_bridge::StdContainerKind::List:
                    case stdlib_bridge::StdContainerKind::Set: {
                        const auto seg =
                            src_container->kind == stdlib_bridge::StdContainerKind::Option
                                ? "optional.inner"
                                : (src_container->kind == stdlib_bridge::StdContainerKind::List
                                       ? "list.element"
                                       : "set.element");
                        const auto child = solver.solve(TypeRelationKind::Subtype,
                                                        *src_container->first,
                                                        *tgt_container->first,
                                                        join_path(path, seg));
                        if (!child) {
                            return child;
                        }
                        if (!solver.witness_is_identity(child.witness)) {
                            ops.push_back(TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::TypeArg,
                                .arg_index = 0,
                                .child = child.witness,
                            });
                        }
                        return solver.accepted_node(source, target, std::move(ops));
                    }
                    case stdlib_bridge::StdContainerKind::Map:
                        if (src_container->second == nullptr || tgt_container->second == nullptr) {
                            return {};
                        }
                        if (!solver.solve(TypeRelationKind::Equivalent,
                                          *src_container->first,
                                          *tgt_container->first,
                                          join_path(path, "map.key"))) {
                            return subtype_leaf(source,
                                                target,
                                                ctx,
                                                join_path(path, "map.key-mismatch"),
                                                solver,
                                                false);
                        }
                        {
                            const auto child = solver.solve(TypeRelationKind::Subtype,
                                                            *src_container->second,
                                                            *tgt_container->second,
                                                            join_path(path, "map.value"));
                            if (!child) {
                                return child;
                            }
                            if (!solver.witness_is_identity(child.witness)) {
                                ops.push_back(TypedAdjustmentOp{
                                    .kind = TypedAdjustmentOpKind::TypeArg,
                                    .arg_index = 1,
                                    .child = child.witness,
                                });
                            }
                            return solver.accepted_node(source, target, std::move(ops));
                        }
                    }
                }
            }
            // KR5.4 (RFC 0013 P5-02): per-parameter variance for user struct
            // nominals. Each argument position is compared according to its
            // declared/inferred variance (Covariant -> subtype, Contravariant
            // -> reversed subtype, Invariant -> equivalence). Absent a variance
            // provider every position defaults to Invariant (legacy behavior).
            std::vector<TypedAdjustmentOp> ops;
            for (std::size_t i = 0; i < s->type_args.size(); ++i) {
                if (s->type_args[i] == nullptr || t->type_args[i] == nullptr) {
                    return {};
                }
                const auto variance = nominal_arg_variance(ctx, s->canonical_name, i);
                const auto child = solve_variant_arg(
                    solver,
                    variance,
                    *s->type_args[i],
                    *t->type_args[i],
                    join_path(path, "struct.type_args[" + std::to_string(i) + "]"));
                if (!child) {
                    return child;
                }
                if (variance != Variance::Invariant && !solver.witness_is_identity(child.witness)) {
                    ops.push_back(TypedAdjustmentOp{
                        .kind = TypedAdjustmentOpKind::TypeArg,
                        .arg_index = static_cast<std::uint32_t>(i),
                        .child = child.witness,
                    });
                }
            }
            return solver.accepted_node(source, target, std::move(ops));
        }
    }

    if (source.holds<types::EnumT>() && target.holds<types::EnumT>()) {
        const auto *s = source.get_if<types::EnumT>();
        const auto *t = target.get_if<types::EnumT>();
        if (s != nullptr && t != nullptr) {
            if (!nominal_name_matches(s->symbol, s->canonical_name, t->symbol, t->canonical_name)) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "enum.name"), solver, false);
            }
            if (s->type_args.size() != t->type_args.size()) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "enum.arity"), solver, false);
            }
            const auto src_container = stdlib_bridge::std_container_type_view(source);
            if (src_container.has_value() && src_container->nominal &&
                src_container->kind == stdlib_bridge::StdContainerKind::Option &&
                src_container->first != nullptr) {
                const auto tgt_container = stdlib_bridge::std_container_type_view(target);
                if (tgt_container.has_value() && tgt_container->nominal &&
                    tgt_container->kind == stdlib_bridge::StdContainerKind::Option &&
                    tgt_container->first != nullptr) {
                    const auto child = solver.solve(TypeRelationKind::Subtype,
                                                    *src_container->first,
                                                    *tgt_container->first,
                                                    join_path(path, "optional.inner"));
                    if (!child) {
                        return child;
                    }
                    std::vector<TypedAdjustmentOp> ops;
                    if (!solver.witness_is_identity(child.witness)) {
                        ops.push_back(TypedAdjustmentOp{
                            .kind = TypedAdjustmentOpKind::TypeArg,
                            .arg_index = 0,
                            .child = child.witness,
                        });
                    }
                    return solver.accepted_node(source, target, std::move(ops));
                }
            }
            // KR5.4 (RFC 0013 P5-02): per-parameter variance for user enum
            // nominals (Result and other user enums). Same rule as structs;
            // defaults to Invariant without a variance provider.
            std::vector<TypedAdjustmentOp> ops;
            for (std::size_t i = 0; i < s->type_args.size(); ++i) {
                if (s->type_args[i] == nullptr || t->type_args[i] == nullptr) {
                    return {};
                }
                const auto variance = nominal_arg_variance(ctx, s->canonical_name, i);
                const auto child =
                    solve_variant_arg(solver,
                                      variance,
                                      *s->type_args[i],
                                      *t->type_args[i],
                                      join_path(path, "enum.type_args[" + std::to_string(i) + "]"));
                if (!child) {
                    return child;
                }
                if (variance != Variance::Invariant && !solver.witness_is_identity(child.witness)) {
                    ops.push_back(TypedAdjustmentOp{
                        .kind = TypedAdjustmentOpKind::TypeArg,
                        .arg_index = static_cast<std::uint32_t>(i),
                        .child = child.witness,
                    });
                }
            }
            return solver.accepted_node(source, target, std::move(ops));
        }
    }

    if (const auto *variant = source.get_if<types::EnumVariantT>();
        variant != nullptr && target.holds<types::EnumT>()) {
        const auto *target_enum = target.get_if<types::EnumT>();
        const bool same_enum = target_enum != nullptr && [&] {
            if (variant->symbol.has_value() && target_enum->symbol.has_value()) {
                return *variant->symbol == *target_enum->symbol;
            }
            return variant->canonical_name == target_enum->canonical_name;
        }();
        if (!same_enum) {
            return subtype_leaf(
                source, target, ctx, join_path(path, "enum.variant"), solver, false);
        }
        // Type arguments must be pairwise equivalent for variant-to-enum subtyping.
        if (variant->type_args.size() != target_enum->type_args.size()) {
            return subtype_leaf(
                source, target, ctx, join_path(path, "enum.variant"), solver, false);
        }
        for (std::size_t i = 0; i < variant->type_args.size(); ++i) {
            if (variant->type_args[i] == nullptr || target_enum->type_args[i] == nullptr) {
                return {};
            }
            if (!solver.solve(
                    TypeRelationKind::Equivalent,
                    *variant->type_args[i],
                    *target_enum->type_args[i],
                    join_path(path, "enum.variant.type_args[" + std::to_string(i) + "]"))) {
                return {};
            }
        }
        return subtype_leaf(source,
                            target,
                            ctx,
                            join_path(path, "enum.variant"),
                            solver,
                            true,
                            {TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::VariantToEnum,
                                .arg_index = 0,
                                .child = kInvalidAdjustmentNode,
                            }});
    }

    // Fn type subtyping: contravariant params, covariant return, covariant effect
    // (weaker effect = subtype of stronger effect).
    if (source.holds<types::FnT>() && target.holds<types::FnT>()) {
        const auto *s = source.get_if<types::FnT>();
        const auto *t = target.get_if<types::FnT>();
        if (s != nullptr && t != nullptr && s->params.size() == t->params.size()) {
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, source, target);
            std::vector<TypedAdjustmentOp> ops;
            // Parameters: contravariant — target.param <: source.param
            for (std::size_t i = 0; i < s->params.size(); ++i) {
                if (s->params[i] == nullptr || t->params[i] == nullptr) {
                    return subtype_leaf(source,
                                        target,
                                        ctx,
                                        join_path(path, "fn.param[" + std::to_string(i) + "]"),
                                        solver,
                                        false);
                }
                const auto child =
                    solver.solve(TypeRelationKind::Subtype,
                                 *t->params[i],
                                 *s->params[i],
                                 join_path(path, "fn.param[" + std::to_string(i) + "]"));
                if (!child) {
                    return child;
                }
                if (!solver.witness_is_identity(child.witness)) {
                    ops.push_back(TypedAdjustmentOp{
                        .kind = TypedAdjustmentOpKind::FnParam,
                        .arg_index = static_cast<std::uint32_t>(i),
                        .child = child.witness,
                    });
                }
            }
            // Return type: covariant — source.return <: target.return
            if (s->return_type == nullptr || t->return_type == nullptr) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "fn.return-null"), solver, false);
            }
            const auto return_child = solver.solve(TypeRelationKind::Subtype,
                                                   *s->return_type,
                                                   *t->return_type,
                                                   join_path(path, "fn.return"));
            if (!return_child) {
                return return_child;
            }
            if (!solver.witness_is_identity(return_child.witness)) {
                ops.push_back(TypedAdjustmentOp{
                    .kind = TypedAdjustmentOpKind::FnReturn,
                    .arg_index = 0,
                    .child = return_child.witness,
                });
            }
            // Effect: covariant — source.effect ⊑ target.effect (source is no
            // stronger than target, so it's acceptable where target is expected).
            if (!judgement_le(s->effect, t->effect)) {
                return subtype_leaf(
                    source, target, ctx, join_path(path, "fn.effect"), solver, false);
            }
            return solver.accepted_node(source, target, std::move(ops));
        }
    }

    // BoundedInt <: Int relaxation.
    const auto *src_bi = source.get_if<types::BoundedIntT>();
    if (src_bi != nullptr && target.holds<types::IntT>()) {
        return subtype_leaf(source,
                            target,
                            ctx,
                            join_path(path, "bounded-int->int"),
                            solver,
                            true,
                            {TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::IntWiden,
                                .arg_index = 0,
                                .child = kInvalidAdjustmentNode,
                            }});
    }

    // BoundedInt covariance.
    if (src_bi != nullptr) {
        const auto *tgt_bi = target.get_if<types::BoundedIntT>();
        if (tgt_bi != nullptr) {
            bool ok = src_bi->minimum >= tgt_bi->minimum && src_bi->maximum <= tgt_bi->maximum;
            return subtype_leaf(source,
                                target,
                                ctx,
                                join_path(path, "bounded-int.bounds"),
                                solver,
                                ok,
                                ok ? std::vector<TypedAdjustmentOp>{TypedAdjustmentOp{
                                         .kind = TypedAdjustmentOpKind::IntWiden,
                                         .arg_index = 0,
                                         .child = kInvalidAdjustmentNode,
                                     }}
                                   : std::vector<TypedAdjustmentOp>{});
        }
    }

    // BoundedString <: String relaxation.
    const auto *src_bs = source.get_if<types::BoundedStringT>();
    const bool allow_bs = ctx == nullptr ? true : ctx->options().allow_bounded_string_relaxation;
    if (allow_bs && src_bs != nullptr && target.holds<types::StringT>()) {
        return subtype_leaf(source,
                            target,
                            ctx,
                            join_path(path, "bounded->string"),
                            solver,
                            true,
                            {TypedAdjustmentOp{
                                .kind = TypedAdjustmentOpKind::StringWiden,
                                .arg_index = 0,
                                .child = kInvalidAdjustmentNode,
                            }});
    }

    // BoundedString covariance.
    if (allow_bs && src_bs != nullptr) {
        const auto *tgt_bs = target.get_if<types::BoundedStringT>();
        if (tgt_bs != nullptr) {
            bool ok = src_bs->minimum >= tgt_bs->minimum && src_bs->maximum <= tgt_bs->maximum;
            return subtype_leaf(source,
                                target,
                                ctx,
                                join_path(path, "bounded.bounds"),
                                solver,
                                ok,
                                ok ? std::vector<TypedAdjustmentOp>{TypedAdjustmentOp{
                                         .kind = TypedAdjustmentOpKind::StringWiden,
                                         .arg_index = 0,
                                         .child = kInvalidAdjustmentNode,
                                     }}
                                   : std::vector<TypedAdjustmentOp>{});
        }
    }

    // Numeric widening (Int -> Float). Disabled by default; callers must
    // explicitly opt in for compatibility or non-source-level analyses.
    const bool allow_numeric = ctx == nullptr ? true : ctx->options().allow_numeric_widening;
    const bool source_is_int_like = source.holds<types::IntT>() || src_bi != nullptr;
    if (allow_numeric && source_is_int_like && target.holds<types::FloatT>()) {
        if (solver.produces_witness()) {
            return solver.fail(RelationFailure::UnsupportedAdjustment);
        }
        return subtype_leaf(source, target, ctx, join_path(path, "numeric.widen"), solver, true);
    }

    // Int <: Decimal (numeric promotion).
    if (allow_numeric && source_is_int_like && target.holds<types::DecimalT>()) {
        if (solver.produces_witness()) {
            return solver.fail(RelationFailure::UnsupportedAdjustment);
        }
        return subtype_leaf(
            source, target, ctx, join_path(path, "numeric.int->decimal"), solver, true);
    }
    // Decimal(s1) <: Decimal(s2) if s2 >= s1 (wider scale accepts narrower).
    if (allow_numeric && source.holds<types::DecimalT>() && target.holds<types::DecimalT>()) {
        const auto *s = source.get_if<types::DecimalT>();
        const auto *t = target.get_if<types::DecimalT>();
        if (s != nullptr && t != nullptr) {
            const bool ok = t->scale >= s->scale;
            if (ok && solver.produces_witness()) {
                return solver.fail(RelationFailure::UnsupportedAdjustment);
            }
            return subtype_leaf(
                source, target, ctx, join_path(path, "numeric.decimal-widen"), solver, ok);
        }
    }

    return {};
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

std::string_view to_string(SchemaBoundaryKind kind) noexcept {
    switch (kind) {
    case SchemaBoundaryKind::AgentInput:
        return "agent input";
    case SchemaBoundaryKind::AgentOutput:
        return "agent output";
    case SchemaBoundaryKind::AgentContextDefault:
        return "agent context default";
    case SchemaBoundaryKind::WorkflowInput:
        return "workflow input";
    case SchemaBoundaryKind::WorkflowOutput:
        return "workflow output";
    case SchemaBoundaryKind::WorkflowNodeInput:
        return "workflow node input";
    }

    return "schema boundary";
}

// ---- Skeleton rendering ----------------------------------------------------

std::string TypeConstraintSkeleton::to_string() const {
    std::ostringstream out;
    dump_node(out, root, 0);
    return out.str();
}

std::size_t RelationKeyHash::operator()(const RelationKey &key) const noexcept {
    std::size_t seed = std::hash<const Type *>{}(key.source);
    const auto mix = [&seed](std::size_t value) noexcept {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    };
    mix(std::hash<const Type *>{}(key.target));
    mix(std::hash<int>{}(static_cast<int>(key.kind)));
    mix(std::hash<bool>{}(key.allow_bounded_string_relaxation));
    mix(std::hash<bool>{}(key.allow_numeric_widening));
    return seed;
}

RelationKey MemoizedRelationSolver::make_key(TypeRelationKind kind,
                                             const Type &source,
                                             const Type &target) const noexcept {
    return RelationKey{
        .kind = kind,
        .source = &source,
        .target = &target,
        .allow_bounded_string_relaxation = ctx_->options().allow_bounded_string_relaxation,
        .allow_numeric_widening = ctx_->options().allow_numeric_widening,
    };
}

bool MemoizedRelationSolver::equivalent(const Type &lhs, const Type &rhs) {
    return solve(TypeRelationKind::Equivalent, lhs, rhs).accepted;
}

bool MemoizedRelationSolver::subtype(const Type &source, const Type &target) {
    return solve(TypeRelationKind::Subtype, source, target).accepted;
}

bool MemoizedRelationSolver::assignable(const Type &source, const Type &target) {
    return solve(TypeRelationKind::Assignable, source, target).accepted;
}

bool MemoizedRelationSolver::exact_schema(const Type &source, const Type &target) {
    return solve(TypeRelationKind::ExactSchema, source, target).accepted;
}

RelationDecision MemoizedRelationSolver::accepted_node(const Type &source,
                                                       const Type &target,
                                                       std::vector<TypedAdjustmentOp> ops) {
    if (!produce_witness_) {
        return RelationDecision{.accepted = true};
    }
    if (witness_nodes_.size() >= kInvalidAdjustmentNode) {
        return fail(RelationFailure::UnsupportedAdjustment);
    }
    const auto id = static_cast<std::uint32_t>(witness_nodes_.size());
    witness_nodes_.push_back(TypedAdjustmentNode{
        .source = &source,
        .target = &target,
        .ops = std::move(ops),
    });
    return RelationDecision{.accepted = true, .witness = id};
}

RelationDecision MemoizedRelationSolver::fail(RelationFailure failure) noexcept {
    if (failure != RelationFailure::None) {
        fatal_failure_ = failure;
    }
    return RelationDecision{
        .accepted = false, .witness = kInvalidAdjustmentNode, .failure = failure};
}

bool MemoizedRelationSolver::witness_is_identity(std::uint32_t witness) const noexcept {
    if (!produce_witness_) {
        return true;
    }
    return witness < witness_nodes_.size() && witness_nodes_[witness].ops.empty();
}

std::optional<TypedAdjustmentPlan> MemoizedRelationSolver::materialize(const Type &source,
                                                                       const Type &target,
                                                                       std::uint32_t root) const {
    if (!produce_witness_ || root >= witness_nodes_.size()) {
        return std::nullopt;
    }

    TypedAdjustmentPlan plan;
    plan.source = &source;
    plan.target = &target;
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    std::vector<unsigned char> color(witness_nodes_.size(), 0);

    std::function<std::optional<std::uint32_t>(std::uint32_t)> copy_node =
        [&](std::uint32_t old_id) -> std::optional<std::uint32_t> {
        if (old_id >= witness_nodes_.size()) {
            return std::nullopt;
        }
        if (color[old_id] == 1) {
            return std::nullopt;
        }
        if (color[old_id] == 2) {
            return remap.at(old_id);
        }
        color[old_id] = 1;
        const auto new_id = static_cast<std::uint32_t>(plan.nodes.size());
        remap.emplace(old_id, new_id);
        const auto &source_node = witness_nodes_[old_id];
        plan.nodes.push_back(TypedAdjustmentNode{
            .source = source_node.source,
            .target = source_node.target,
            .ops = source_node.ops,
        });
        for (std::size_t op_index = 0; op_index < source_node.ops.size(); ++op_index) {
            const auto old_child = source_node.ops[op_index].child;
            if (old_child == kInvalidAdjustmentNode) {
                continue;
            }
            const auto child = copy_node(old_child);
            if (!child.has_value()) {
                return std::nullopt;
            }
            plan.nodes[new_id].ops[op_index].child = *child;
        }
        color[old_id] = 2;
        return new_id;
    };

    const auto materialized_root = copy_node(root);
    if (!materialized_root.has_value()) {
        return std::nullopt;
    }
    plan.root = *materialized_root;
    return plan;
}

RelationDecision MemoizedRelationSolver::solve(TypeRelationKind kind,
                                               const Type &source,
                                               const Type &target,
                                               std::string path) {
    if (recursion_depth_ == 0) {
        fatal_failure_ = RelationFailure::None;
    }
    ++stats_.queries;
    const auto key = make_key(kind, source, target);
    if (const auto iter = memo_.find(key); iter != memo_.end()) {
        switch (iter->second.state) {
        case RelationState::Proven:
            ++stats_.cache_hits;
            return RelationDecision{.accepted = true, .witness = iter->second.witness};
        case RelationState::Disproven:
            ++stats_.cache_hits;
            return RelationDecision{.accepted = false,
                                    .witness = kInvalidAdjustmentNode,
                                    .failure = iter->second.failure};
        case RelationState::Visiting:
            if (produce_witness_) {
                ++stats_.witness_cycle_rejections;
                return fail(RelationFailure::VisitingCycle);
            }
            ++stats_.coinductive_assumptions;
            if (ctx_->options().enable_trace) {
                sync_trace(ctx_,
                           kind,
                           path,
                           target.describe(),
                           source.describe(),
                           true,
                           "coinductive relation assumption");
            }
            return RelationDecision{.accepted = true};
        }
    }

    if (recursion_depth_ >= ctx_->options().max_solver_depth) {
        ++stats_.depth_guard_rejections;
        ++stats_.disproven;
        const auto failure = produce_witness_ ? RelationFailure::DepthLimit : RelationFailure::None;
        memo_.emplace(key,
                      RelationMemoEntry{
                          .state = RelationState::Disproven,
                          .witness = kInvalidAdjustmentNode,
                          .failure = failure,
                      });
        if (ctx_->options().enable_trace) {
            sync_trace(ctx_,
                       kind,
                       path,
                       target.describe(),
                       source.describe(),
                       false,
                       "relation depth limit exceeded");
        }
        return produce_witness_ ? fail(failure) : RelationDecision{};
    }

    memo_.emplace(key, RelationMemoEntry{.state = RelationState::Visiting});
    ++recursion_depth_;
    RelationDecision decision;
    switch (kind) {
    case TypeRelationKind::Equivalent:
        if (equivalent_impl(source, target, ctx_, path, *this)) {
            decision = accepted_node(source, target);
        } else if (fatal_failure_ != RelationFailure::None) {
            decision = fail(fatal_failure_);
        }
        break;
    case TypeRelationKind::Subtype:
        decision = subtype_impl(source, target, ctx_, path, *this);
        break;
    case TypeRelationKind::Assignable:
        decision = subtype_impl(source, target, ctx_, path, *this);
        if (ctx_->options().enable_trace) {
            TypeRelationTraceStep step;
            step.kind = TypeRelationKind::Assignable;
            step.depth = 0;
            step.path = path;
            step.expected_describe = target.describe();
            step.actual_describe = source.describe();
            step.result =
                decision.accepted ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
            step.reason = "assignability delegated to subtype check";
            ctx_->trace().steps.push_back(std::move(step));
        }
        break;
    case TypeRelationKind::ExactSchema:
        if (equivalent_impl(source, target, ctx_, path, *this)) {
            decision = accepted_node(source, target);
        } else if (fatal_failure_ != RelationFailure::None) {
            decision = fail(fatal_failure_);
        }
        if (ctx_->options().enable_trace) {
            TypeRelationTraceStep step;
            step.kind = TypeRelationKind::ExactSchema;
            step.depth = 0;
            step.path = path;
            step.expected_describe = target.describe();
            step.actual_describe = source.describe();
            step.result =
                decision.accepted ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
            step.reason = "exact schema match implemented as equivalence";
            ctx_->trace().steps.push_back(std::move(step));
        }
        break;
    }
    if (!decision.accepted && decision.failure == RelationFailure::None &&
        fatal_failure_ != RelationFailure::None) {
        decision.failure = fatal_failure_;
    }
    --recursion_depth_;

    if (const auto iter = memo_.find(key); iter != memo_.end()) {
        iter->second = RelationMemoEntry{
            .state = decision.accepted ? RelationState::Proven : RelationState::Disproven,
            .witness = decision.witness,
            .failure = decision.failure,
        };
    }
    if (decision.accepted) {
        ++stats_.proven;
    } else {
        ++stats_.disproven;
    }
    return decision;
}

// ---- Equivalence public API ------------------------------------------------

bool are_types_equivalent(const Type &lhs, const Type &rhs, TypeRelationContext &ctx) {
    if (ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = lhs.describe();
        top.right_describe = rhs.describe();
        ctx.push_node(std::move(top));
    }

    MemoizedRelationSolver solver(ctx);
    const bool result = solver.equivalent(lhs, rhs);

    if (ctx.options().emit_constraint_skeleton) {
        // pop the synthetic top-level And frame (it aggregates children).
        ctx.pop_node();
    }
    return result;
}

bool are_types_equivalent(const Type &lhs, const Type &rhs) {
    TypeRelationContext ctx;
    return are_types_equivalent(lhs, rhs, ctx);
}

// ---- Subtype public API ----------------------------------------------------

bool is_subtype_of(const Type &source, const Type &target, TypeRelationContext &ctx) {
    if (ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    MemoizedRelationSolver solver(ctx);
    const bool result = solver.subtype(source, target);

    if (ctx.options().emit_constraint_skeleton) {
        ctx.pop_node();
    }
    return result;
}

bool is_subtype_of(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return is_subtype_of(source, target, ctx);
}

// ---- Assignable = subtype --------------------------------------------------

bool is_assignable_to(const Type &source, const Type &target, TypeRelationContext &ctx) {
    if (ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    MemoizedRelationSolver solver(ctx);
    const bool result = solver.assignable(source, target);

    if (ctx.options().emit_constraint_skeleton) {
        ctx.pop_node();
    }
    return result;
}

AssignabilityDecision
decide_assignability(const Type &source, const Type &target, TypeRelationContext &ctx) {
    if (ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    MemoizedRelationSolver solver(ctx, true);
    const auto relation =
        solver.solve(TypeRelationKind::Assignable, source, target, "annotated-let");

    if (ctx.options().emit_constraint_skeleton) {
        ctx.pop_node();
    }
    if (!relation.accepted) {
        return AssignabilityDecision{
            .accepted = false,
            .adjustment = std::nullopt,
            .failure = relation.failure,
        };
    }
    auto plan = solver.materialize(source, target, relation.witness);
    if (!plan.has_value()) {
        return AssignabilityDecision{
            .accepted = false,
            .adjustment = std::nullopt,
            .failure = RelationFailure::UnsupportedAdjustment,
        };
    }
    return AssignabilityDecision{
        .accepted = true,
        .adjustment = std::move(plan),
        .failure = RelationFailure::None,
    };
}

bool is_assignable_to(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return is_assignable_to(source, target, ctx);
}

// ---- Bounded collection capacity overflow ----------------------------------

std::optional<std::pair<std::uint64_t, std::uint64_t>>
collection_capacity_overflow(const Type &source, const Type &target, TypeRelationContext &ctx) {
    const auto *source_struct = source.get_if<types::StructT>();
    const auto *target_struct = target.get_if<types::StructT>();
    if (source_struct == nullptr || target_struct == nullptr) {
        return std::nullopt;
    }
    if (!source_struct->capacity.has_value() || !target_struct->capacity.has_value()) {
        return std::nullopt;
    }
    if (*source_struct->capacity <= *target_struct->capacity) {
        return std::nullopt;
    }
    const auto source_view = stdlib_bridge::std_container_type_view(source);
    const auto target_view = stdlib_bridge::std_container_type_view(target);
    if (!source_view.has_value() || !target_view.has_value() ||
        source_view->kind != target_view->kind) {
        return std::nullopt;
    }
    // The element (and, for Map, key/value) types must themselves be assignable
    // so capacity is provably the only reason for the failure.
    if (source_view->first != nullptr && target_view->first != nullptr &&
        !is_assignable_to(*source_view->first, *target_view->first, ctx)) {
        return std::nullopt;
    }
    if (source_view->second != nullptr && target_view->second != nullptr &&
        !is_assignable_to(*source_view->second, *target_view->second, ctx)) {
        return std::nullopt;
    }
    return std::make_pair(*source_struct->capacity, *target_struct->capacity);
}

std::optional<std::pair<std::uint64_t, std::uint64_t>>
collection_capacity_overflow(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return collection_capacity_overflow(source, target, ctx);
}

// ---- Exact schema match ----------------------------------------------------

bool is_exact_schema_match(const Type &source, const Type &target, TypeRelationContext &ctx) {
    if (ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    MemoizedRelationSolver solver(ctx);
    const bool result = solver.exact_schema(source, target);

    if (ctx.options().emit_constraint_skeleton) {
        ctx.pop_node();
    }
    return result;
}

bool is_exact_schema_match(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return is_exact_schema_match(source, target, ctx);
}

} // namespace ahfl

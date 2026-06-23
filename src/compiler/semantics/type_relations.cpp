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

std::optional<bool> equivalent_std_container_bridge(const Type &lhs,
                                                    const Type &rhs,
                                                    TypeRelationContext *ctx,
                                                    const std::string &path,
                                                    MemoizedRelationSolver &solver) {
    const auto lhs_view = stdlib_bridge::std_container_type_view(lhs);
    const auto rhs_view = stdlib_bridge::std_container_type_view(rhs);
    if (!lhs_view.has_value() || !rhs_view.has_value() || lhs_view->kind != rhs_view->kind) {
        return std::nullopt;
    }

    FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, lhs, rhs);
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
        return equivalent_pairwise(*lhs_view->first,
                                   *rhs_view->first,
                                   *lhs_view->second,
                                   *rhs_view->second,
                                   path,
                                   "map.key",
                                   "map.value",
                                   solver);
    }

    return std::nullopt;
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

    if (const auto bridged = equivalent_std_container_bridge(lhs, rhs, ctx, path, solver);
        bridged.has_value()) {
        return *bridged;
    }

    if (lhs.payload.index() != rhs.payload.index()) {
        return equivalent_leaf(lhs, rhs, ctx, path, false);
    }

    return lhs.visit(types::Overloads{
        [&](const types::AnyT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::NeverT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::ErrorT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::UnitT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::BoolT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
        [&](const types::IntT &) { return equivalent_leaf(lhs, rhs, ctx, path, true); },
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
            return true;
        },
        [&](const types::EnumT &l) {
            const auto *r = rhs.get_if<types::EnumT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            const bool name_ok =
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
            // as a defensive sanity check — the global TypeContext interns
            // TypeVars by (index, name) so the pointers are already identical
            // for equivalent vars.
            const auto *r = rhs.get_if<types::TypeVarT>();
            bool eq = r != nullptr && l.index == r->index && l.name == r->name;
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const auto &) { return equivalent_leaf(lhs, rhs, ctx, path, false); },
    });
}

// ---- Subtype ---------------------------------------------------------------

bool subtype_impl(const Type &source,
                  const Type &target,
                  TypeRelationContext *ctx,
                  const std::string &path,
                  MemoizedRelationSolver &solver);

bool subtype_leaf(const Type &source,
                  const Type &target,
                  TypeRelationContext *ctx,
                  const std::string &path,
                  bool value) {
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
    return value;
}

std::optional<bool> subtype_std_container_bridge(const Type &source,
                                                 const Type &target,
                                                 TypeRelationContext *ctx,
                                                 const std::string &path,
                                                 MemoizedRelationSolver &solver) {
    const auto source_view = stdlib_bridge::std_container_type_view(source);
    const auto target_view = stdlib_bridge::std_container_type_view(target);
    if (!source_view.has_value() || !target_view.has_value() ||
        source_view->kind != target_view->kind) {
        return std::nullopt;
    }

    switch (source_view->kind) {
    case stdlib_bridge::StdContainerKind::Option:
        return solver.solve(TypeRelationKind::Subtype,
                            *source_view->first,
                            *target_view->first,
                            join_path(path, "optional.inner"));
    case stdlib_bridge::StdContainerKind::List:
        return solver.solve(TypeRelationKind::Subtype,
                            *source_view->first,
                            *target_view->first,
                            join_path(path, "list.element"));
    case stdlib_bridge::StdContainerKind::Set:
        return solver.solve(TypeRelationKind::Subtype,
                            *source_view->first,
                            *target_view->first,
                            join_path(path, "set.element"));
    case stdlib_bridge::StdContainerKind::Map:
        if (!solver.solve(TypeRelationKind::Equivalent,
                          *source_view->first,
                          *target_view->first,
                          join_path(path, "map.key"))) {
            return subtype_leaf(source, target, ctx, join_path(path, "map.key-mismatch"), false);
        }
        return solver.solve(TypeRelationKind::Subtype,
                            *source_view->second,
                            *target_view->second,
                            join_path(path, "map.value"));
    }

    return std::nullopt;
}

bool subtype_impl(const Type &source,
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
        return subtype_leaf(source, target, ctx, join_path(path, "identical"), true);
    }

    // Error propagation: Error is both top-and-bottom for error recovery — it is
    // compatible with every type in both directions so a single error doesn't
    // cascade into dozens of spurious secondary diagnostics.
    if (source.holds<types::ErrorT>() || target.holds<types::ErrorT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "error"), true);
    }

    // Any is the top type: every type is a subtype of Any.
    if (target.holds<types::AnyT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "any-top"), true);
    }

    // Never is the bottom type: Never is a subtype of every type.
    if (source.holds<types::NeverT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "never-bottom"), true);
    }

    // Branch 1: equivalence. Relation kind is part of the solver key, so
    // equivalence and subtype cache entries stay separate.
    const bool eq =
        solver.solve(TypeRelationKind::Equivalent, source, target, join_path(path, "equiv"));
    if (eq) {
        return true;
    }

    if (const auto bridged = subtype_std_container_bridge(source, target, ctx, path, solver);
        bridged.has_value()) {
        return *bridged;
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
            return subtype_leaf(source, target, ctx, join_path(path, "enum.variant"), false);
        }
        // Type arguments must be pairwise equivalent for variant-to-enum subtyping.
        if (variant->type_args.size() != target_enum->type_args.size()) {
            return subtype_leaf(source, target, ctx, join_path(path, "enum.variant"), false);
        }
        for (std::size_t i = 0; i < variant->type_args.size(); ++i) {
            if (variant->type_args[i] == nullptr || target_enum->type_args[i] == nullptr) {
                return false;
            }
            if (!solver.solve(
                    TypeRelationKind::Equivalent,
                    *variant->type_args[i],
                    *target_enum->type_args[i],
                    join_path(path, "enum.variant.type_args[" + std::to_string(i) + "]"))) {
                return false;
            }
        }
        return subtype_leaf(source, target, ctx, join_path(path, "enum.variant"), true);
    }

    // Fn type subtyping: contravariant params, covariant return, covariant effect
    // (weaker effect = subtype of stronger effect).
    if (source.holds<types::FnT>() && target.holds<types::FnT>()) {
        const auto *s = source.get_if<types::FnT>();
        const auto *t = target.get_if<types::FnT>();
        if (s != nullptr && t != nullptr && s->params.size() == t->params.size()) {
            FrameGuard guard(ctx, TypeConstraintNode::Kind::And, path, source, target);
            // Parameters: contravariant — target.param <: source.param
            for (std::size_t i = 0; i < s->params.size(); ++i) {
                if (s->params[i] == nullptr || t->params[i] == nullptr) {
                    return subtype_leaf(source,
                                        target,
                                        ctx,
                                        join_path(path, "fn.param[" + std::to_string(i) + "]"),
                                        false);
                }
                if (!solver.solve(TypeRelationKind::Subtype,
                                  *t->params[i],
                                  *s->params[i],
                                  join_path(path, "fn.param[" + std::to_string(i) + "]"))) {
                    return false;
                }
            }
            // Return type: covariant — source.return <: target.return
            if (s->return_type == nullptr || t->return_type == nullptr) {
                return subtype_leaf(source, target, ctx, join_path(path, "fn.return-null"), false);
            }
            if (!solver.solve(TypeRelationKind::Subtype,
                              *s->return_type,
                              *t->return_type,
                              join_path(path, "fn.return"))) {
                return false;
            }
            // Effect: covariant — source.effect ⊑ target.effect (source is no
            // stronger than target, so it's acceptable where target is expected).
            if (!judgement_le(s->effect, t->effect)) {
                return subtype_leaf(source, target, ctx, join_path(path, "fn.effect"), false);
            }
            return true;
        }
    }

    // BoundedString <: String relaxation.
    const auto *src_bs = source.get_if<types::BoundedStringT>();
    const bool allow_bs = ctx == nullptr ? true : ctx->options().allow_bounded_string_relaxation;
    if (allow_bs && src_bs != nullptr && target.holds<types::StringT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "bounded->string"), true);
    }

    // BoundedString covariance.
    if (allow_bs && src_bs != nullptr) {
        const auto *tgt_bs = target.get_if<types::BoundedStringT>();
        if (tgt_bs != nullptr) {
            bool ok = src_bs->minimum >= tgt_bs->minimum && src_bs->maximum <= tgt_bs->maximum;
            return subtype_leaf(source, target, ctx, join_path(path, "bounded.bounds"), ok);
        }
    }

    // Numeric widening (Int -> Float). Disabled by default; callers must
    // explicitly opt in for compatibility or non-source-level analyses.
    const bool allow_numeric = ctx == nullptr ? true : ctx->options().allow_numeric_widening;
    if (allow_numeric && source.holds<types::IntT>() && target.holds<types::FloatT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "numeric.widen"), true);
    }

    // Int <: Decimal (numeric promotion).
    if (allow_numeric && source.holds<types::IntT>() && target.holds<types::DecimalT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "numeric.int->decimal"), true);
    }
    // Decimal(s1) <: Decimal(s2) if s2 >= s1 (wider scale accepts narrower).
    if (allow_numeric && source.holds<types::DecimalT>() && target.holds<types::DecimalT>()) {
        const auto *s = source.get_if<types::DecimalT>();
        const auto *t = target.get_if<types::DecimalT>();
        if (s != nullptr && t != nullptr) {
            const bool ok = t->scale >= s->scale;
            return subtype_leaf(source, target, ctx, join_path(path, "numeric.decimal-widen"), ok);
        }
    }

    return false;
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
    return solve(TypeRelationKind::Equivalent, lhs, rhs);
}

bool MemoizedRelationSolver::subtype(const Type &source, const Type &target) {
    return solve(TypeRelationKind::Subtype, source, target);
}

bool MemoizedRelationSolver::assignable(const Type &source, const Type &target) {
    return solve(TypeRelationKind::Assignable, source, target);
}

bool MemoizedRelationSolver::exact_schema(const Type &source, const Type &target) {
    return solve(TypeRelationKind::ExactSchema, source, target);
}

bool MemoizedRelationSolver::solve(TypeRelationKind kind,
                                   const Type &source,
                                   const Type &target,
                                   std::string path) {
    ++stats_.queries;
    const auto key = make_key(kind, source, target);
    if (const auto iter = memo_.find(key); iter != memo_.end()) {
        switch (iter->second) {
        case RelationState::Proven:
            ++stats_.cache_hits;
            return true;
        case RelationState::Disproven:
            ++stats_.cache_hits;
            return false;
        case RelationState::Visiting:
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
            return true;
        }
    }

    if (recursion_depth_ >= ctx_->options().max_solver_depth) {
        ++stats_.depth_guard_rejections;
        ++stats_.disproven;
        memo_.emplace(key, RelationState::Disproven);
        if (ctx_->options().enable_trace) {
            sync_trace(ctx_,
                       kind,
                       path,
                       target.describe(),
                       source.describe(),
                       false,
                       "relation depth limit exceeded");
        }
        return false;
    }

    memo_.emplace(key, RelationState::Visiting);
    ++recursion_depth_;
    bool result = false;
    switch (kind) {
    case TypeRelationKind::Equivalent:
        result = equivalent_impl(source, target, ctx_, path, *this);
        break;
    case TypeRelationKind::Subtype:
        result = subtype_impl(source, target, ctx_, path, *this);
        break;
    case TypeRelationKind::Assignable:
        result = subtype_impl(source, target, ctx_, path, *this);
        if (ctx_->options().enable_trace) {
            TypeRelationTraceStep step;
            step.kind = TypeRelationKind::Assignable;
            step.depth = 0;
            step.path = path;
            step.expected_describe = target.describe();
            step.actual_describe = source.describe();
            step.result = result ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
            step.reason = "assignability delegated to subtype check";
            ctx_->trace().steps.push_back(std::move(step));
        }
        break;
    case TypeRelationKind::ExactSchema:
        result = equivalent_impl(source, target, ctx_, path, *this);
        if (ctx_->options().enable_trace) {
            TypeRelationTraceStep step;
            step.kind = TypeRelationKind::ExactSchema;
            step.depth = 0;
            step.path = path;
            step.expected_describe = target.describe();
            step.actual_describe = source.describe();
            step.result = result ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
            step.reason = "exact schema match implemented as equivalence";
            ctx_->trace().steps.push_back(std::move(step));
        }
        break;
    }
    --recursion_depth_;

    if (const auto iter = memo_.find(key); iter != memo_.end()) {
        iter->second = result ? RelationState::Proven : RelationState::Disproven;
    }
    if (result) {
        ++stats_.proven;
    } else {
        ++stats_.disproven;
    }
    return result;
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

bool is_assignable_to(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return is_assignable_to(source, target, ctx);
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

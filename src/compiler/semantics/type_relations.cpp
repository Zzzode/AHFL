#include "ahfl/compiler/semantics/type_relations.hpp"

#include <set>
#include <sstream>

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

// ---- Cycle detection (occurs check) ----------------------------------------
//
// The relational traversals recurse into composite types (Optional, List, Set,
// Map).  While the current type system has no way to form a true recursive
// type (Struct/Enum payloads carry only names, not field pointers), deep or
// adversarial nesting can still overflow the stack, and future type-system
// extensions may introduce recursive type aliases.
//
// We protect every recursive call with a visited-set check keyed by the pair
// of raw type pointers.  Because types are hash-consed, pointer identity is
// equivalent to type identity, so a repeat of the same (lhs, rhs) pair means
// we have re-entered the same sub-problem — a cycle.  We treat cycles
// coinductively: a re-entrant query is answered as true ("assume it holds"),
// which matches the greatest-fixed-point semantics of recursive types.

using TypePairSet = std::set<std::pair<const Type *, const Type *>>;

// Insert an ordered pair into the visited set.  Returns false if the pair
// was already present (cycle detected), true if it was freshly inserted.
static bool visited_insert(TypePairSet &visited, const Type *a, const Type *b) {
    return visited.insert({a, b}).second;
}

std::string_view relation_name(std::string_view rel) noexcept {
    if (rel.empty()) {
        return "relation";
    }
    return rel;
}

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

    ~FrameGuard() {
        if (pushed) {
            ctx->pop_node();
        }
    }

    FrameGuard(const FrameGuard &) = delete;
    FrameGuard &operator=(const FrameGuard &) = delete;
};

TypeConstraintNode make_composite_node(TypeConstraintNode::Kind kind,
                                       const std::string &path,
                                       const Type &lhs,
                                       const Type &rhs) {
    TypeConstraintNode n;
    n.kind = kind;
    n.path = path;
    n.left_describe = lhs.describe();
    n.right_describe = rhs.describe();
    return n;
}

// ---- Equivalence -----------------------------------------------------------

bool equivalent_impl(const Type &lhs,
                     const Type &rhs,
                     TypeRelationContext *ctx,
                     const std::string &path,
                     TypePairSet *visited);

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
                         TypeRelationContext *ctx,
                         const std::string &base,
                         std::string_view seg_a,
                         std::string_view seg_b,
                         TypePairSet *visited) {
    const bool ok_a = equivalent_impl(lhs_a, rhs_a, ctx, join_path(base, seg_a), visited);
    const bool ok_b = equivalent_impl(lhs_b, rhs_b, ctx, join_path(base, seg_b), visited);
    return ok_a && ok_b;
}

bool equivalent_impl(const Type &lhs,
                     const Type &rhs,
                     TypeRelationContext *ctx,
                     const std::string &path,
                     TypePairSet *visited) {
    // Pointer identity short-circuit.
    if (&lhs == &rhs) {
        return equivalent_leaf(lhs, rhs, ctx, path, true);
    }

    // Cycle detection (occurs check): if we've already started comparing
    // this pair, assume success (greatest fixed point / coinductive rule).
    if (visited != nullptr && !visited_insert(*visited, &lhs, &rhs)) {
        return equivalent_leaf(lhs, rhs, ctx, join_path(path, "cycle"), true);
    }

    if (lhs.payload.index() != rhs.payload.index()) {
        return equivalent_leaf(lhs, rhs, ctx, path, false);
    }

    return lhs.visit(types::Overloads{
        [&](const types::AnyT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::NeverT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::ErrorT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::UnitT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::BoolT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::IntT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::FloatT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::StringT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::UUIDT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::TimestampT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
        [&](const types::DurationT &) {
            return equivalent_leaf(lhs, rhs, ctx, path, true);
        },
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
            //   AND ( symbol match OR fallback canonical_name match )
            const auto *r = rhs.get_if<types::StructT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }

            FrameGuard guard(ctx,
                             make_composite_node(TypeConstraintNode::Kind::And, path, lhs, rhs));

            bool name_ok = false;
            if (l.symbol.has_value() && r->symbol.has_value()) {
                // Model symbol match as a relation leaf under struct.name.
                bool sym_ok = *l.symbol == *r->symbol;
                if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
                    TypeConstraintNode n;
                    n.kind = TypeConstraintNode::Kind::Relation;
                    n.relation = "equivalent";
                    n.path = join_path(path, "struct.name");
                    n.left_describe = lhs.describe();
                    n.right_describe = rhs.describe();
                    n.satisfied = sym_ok;
                    ctx->push_node(std::move(n));
                    ctx->pop_node();
                }
                name_ok = sym_ok;
            } else {
                bool cname_ok = l.canonical_name == r->canonical_name;
                if (ctx != nullptr && ctx->options().emit_constraint_skeleton) {
                    TypeConstraintNode n;
                    n.kind = TypeConstraintNode::Kind::Relation;
                    n.relation = "equivalent";
                    n.path = join_path(path, "struct.name");
                    n.left_describe = lhs.describe();
                    n.right_describe = rhs.describe();
                    n.satisfied = cname_ok;
                    ctx->push_node(std::move(n));
                    ctx->pop_node();
                }
                name_ok = cname_ok;
            }
            // Note: StructT has no fields in the payload in this codebase
            // version, so the And node has exactly one child (struct.name).
            (void)name_ok;
            return name_ok;
        },
        [&](const types::EnumT &l) {
            const auto *r = rhs.get_if<types::EnumT>();
            if (r == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            bool eq = [&] {
                if (l.symbol.has_value() && r->symbol.has_value()) {
                    return *l.symbol == *r->symbol;
                }
                return l.canonical_name == r->canonical_name;
            }();
            return equivalent_leaf(lhs, rhs, ctx, path, eq);
        },
        [&](const types::OptionalT &l) {
            const auto *r = rhs.get_if<types::OptionalT>();
            if (r == nullptr || l.inner == nullptr || r->inner == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, make_composite_node(
                                      TypeConstraintNode::Kind::And,
                                      path, lhs, rhs));
            return equivalent_impl(*l.inner, *r->inner, ctx,
                                   join_path(path, "optional.inner"), visited);
        },
        [&](const types::ListT &l) {
            const auto *r = rhs.get_if<types::ListT>();
            if (r == nullptr || l.element == nullptr || r->element == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, make_composite_node(
                                      TypeConstraintNode::Kind::And,
                                      path, lhs, rhs));
            return equivalent_impl(*l.element, *r->element, ctx,
                                   join_path(path, "list.element"), visited);
        },
        [&](const types::SetT &l) {
            const auto *r = rhs.get_if<types::SetT>();
            if (r == nullptr || l.element == nullptr || r->element == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, make_composite_node(
                                      TypeConstraintNode::Kind::And,
                                      path, lhs, rhs));
            return equivalent_impl(*l.element, *r->element, ctx,
                                   join_path(path, "set.element"), visited);
        },
        [&](const types::MapT &l) {
            const auto *r = rhs.get_if<types::MapT>();
            if (r == nullptr || l.key == nullptr || r->key == nullptr || l.value == nullptr ||
                r->value == nullptr) {
                return equivalent_leaf(lhs, rhs, ctx, path, false);
            }
            FrameGuard guard(ctx, make_composite_node(
                                      TypeConstraintNode::Kind::And,
                                      path, lhs, rhs));
            return equivalent_pairwise(*l.key, *r->key, *l.value, *r->value,
                                       ctx, path, "map.key", "map.value", visited);
        },
    });
}

// ---- Subtype ---------------------------------------------------------------

bool subtype_impl(const Type &source,
                  const Type &target,
                  TypeRelationContext *ctx,
                  const std::string &path,
                  TypePairSet *visited);

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

bool subtype_impl(const Type &source,
                  const Type &target,
                  TypeRelationContext *ctx,
                  const std::string &path,
                  TypePairSet *visited) {
    // Disjunction: subtype can succeed via equivalence, via top/bottom type
    // rules, via structural covariance, or via any of the specific
    // relaxations. Model this as an Or node when the skeleton is enabled.
    FrameGuard or_guard(nullptr, TypeConstraintNode{});
    TypeConstraintNode or_node;
    or_node.kind = TypeConstraintNode::Kind::Or;
    or_node.path = path;
    or_node.left_describe = source.describe();
    or_node.right_describe = target.describe();
    FrameGuard real_guard(ctx, std::move(or_node));

    // Pointer identity short-circuit.
    if (&source == &target) {
        return subtype_leaf(source, target, ctx, join_path(path, "identical"), true);
    }

    // Cycle detection (occurs check): if we've already started this pair,
    // assume success (greatest fixed point / coinductive rule).
    if (visited != nullptr && !visited_insert(*visited, &source, &target)) {
        return subtype_leaf(source, target, ctx, join_path(path, "cycle"), true);
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

    // Branch 1: equivalence.  Use a separate visited set for the equivalence
    // sub-query — cycle detection is per-relation, not cross-relation.
    TypePairSet equiv_visited;
    const bool eq = equivalent_impl(source, target, ctx, join_path(path, "equiv"),
                                    &equiv_visited);
    if (eq) {
        return true;
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

    // Numeric widening (Int -> Float). Disabled in strict / schema mode.
    const bool allow_numeric = ctx == nullptr ? true : ctx->options().allow_numeric_widening;
    if (allow_numeric && source.holds<types::IntT>() && target.holds<types::FloatT>()) {
        return subtype_leaf(source, target, ctx, join_path(path, "numeric.widen"), true);
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

// ---- Equivalence public API ------------------------------------------------

bool are_types_equivalent(const Type &lhs, const Type &rhs, TypeRelationContext &ctx) {
    // Pass ctx down whenever either skeleton or flat trace is enabled, so
    // sync_trace() inside the impl functions can actually record steps.
    // Previously ctx was passed only when emit_constraint_skeleton was true,
    // which silently dropped all flat trace output when trace_type_relations
    // was on but skeleton was off (the common case).
    const bool wants_ctx = ctx.options().emit_constraint_skeleton || ctx.options().enable_trace;

    if (wants_ctx && ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = lhs.describe();
        top.right_describe = rhs.describe();
        ctx.push_node(std::move(top));
    }

    TypePairSet visited;
    const bool result = equivalent_impl(lhs, rhs, wants_ctx ? &ctx : nullptr,
                                        /*path=*/"", &visited);

    if (wants_ctx && ctx.options().emit_constraint_skeleton) {
        // pop the synthetic top-level And frame (it aggregates children).
        ctx.pop_node();
    }
    (void)relation_name; // keep linkage tidy
    return result;
}

bool are_types_equivalent(const Type &lhs, const Type &rhs) {
    TypeRelationContext ctx;
    return are_types_equivalent(lhs, rhs, ctx);
}

// ---- Subtype public API ----------------------------------------------------

bool is_subtype_of(const Type &source, const Type &target, TypeRelationContext &ctx) {
    const bool wants_ctx = ctx.options().emit_constraint_skeleton || ctx.options().enable_trace ||
                           !ctx.options().allow_bounded_string_relaxation ||
                           !ctx.options().allow_numeric_widening;

    if (wants_ctx && ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    TypePairSet visited;
    const bool result = subtype_impl(source, target, wants_ctx ? &ctx : nullptr,
                                     /*path=*/"", &visited);

    if (wants_ctx && ctx.options().emit_constraint_skeleton) {
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
    // Assignability is modelled as a subtype relation; let subtype_impl own
    // the Or node (equiv path ∪ subtype relaxations).
    const bool wants_ctx = ctx.options().emit_constraint_skeleton || ctx.options().enable_trace ||
                           !ctx.options().allow_bounded_string_relaxation ||
                           !ctx.options().allow_numeric_widening;

    if (wants_ctx && ctx.stack_empty() && ctx.options().emit_constraint_skeleton) {
        TypeConstraintNode top;
        top.kind = TypeConstraintNode::Kind::And;
        top.path = "";
        top.left_describe = source.describe();
        top.right_describe = target.describe();
        ctx.push_node(std::move(top));
    }

    TypeRelationContext *maybe_ctx = wants_ctx ? &ctx : nullptr;
    TypePairSet visited;
    const bool result = subtype_impl(source, target, maybe_ctx, /*path=*/"", &visited);

    // Record an Assignable kind at the top level so consumers of the flat
    // trace can see which relation was actually requested at the API
    // boundary (subtype_impl records its inner steps as Subtype).
    // Top-level steps are always recorded (even on success) so callers can
    // identify which API was exercised.
    if (maybe_ctx != nullptr && ctx.options().enable_trace) {
        TypeRelationTraceStep step;
        step.kind = TypeRelationKind::Assignable;
        step.depth = 0;
        step.path = "";
        step.expected_describe = target.describe();
        step.actual_describe = source.describe();
        step.result = result ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
        step.reason = "assignability delegated to subtype check";
        ctx.trace().steps.push_back(std::move(step));
    }

    if (wants_ctx && ctx.options().emit_constraint_skeleton) {
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
    const bool result = are_types_equivalent(source, target, ctx);
    // Record an ExactSchema kind at the top level so consumers of the flat
    // trace can see which relation was actually requested at the API
    // boundary (are_types_equivalent records its inner steps as Equivalent).
    // Top-level step is always recorded (even on success) so callers can
    // identify which API was exercised.
    const bool wants_trace = ctx.options().emit_constraint_skeleton || ctx.options().enable_trace;
    if (wants_trace && ctx.options().enable_trace) {
        TypeRelationTraceStep step;
        step.kind = TypeRelationKind::ExactSchema;
        step.depth = 0;
        step.path = "";
        step.expected_describe = target.describe();
        step.actual_describe = source.describe();
        step.result = result ? TypeRelationResult::Accepted : TypeRelationResult::Rejected;
        step.reason = "exact schema match implemented as equivalence";
        ctx.trace().steps.push_back(std::move(step));
    }
    return result;
}

bool is_exact_schema_match(const Type &source, const Type &target) {
    TypeRelationContext ctx;
    return is_exact_schema_match(source, target, ctx);
}

} // namespace ahfl

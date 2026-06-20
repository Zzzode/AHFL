#pragma once

#include "ahfl/compiler/semantics/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ahfl {

// ---------------------------------------------------------------------------
// Type constraint skeleton (P1.5)
// ---------------------------------------------------------------------------
//
// When TypeRelationOptions::emit_constraint_skeleton is true, each relational
// check (equivalent / subtype / assignable) builds a tree that mirrors the
// traversal. The tree nodes model:
//
//   And      : a conjunction of sub-constraints that must all be satisfied
//              (e.g. list.shape + list.element, all struct fields + struct.name)
//   Or       : a disjunction of alternative success paths
//              (e.g. optional unwrap branches, subtype fallbacks)
//   Relation : a direct pairwise check (a named relation between two types)
//   Leaf     : a trivial terminal result
//
// Every Relation and Leaf node also carries the boolean outcome on its
// `satisfied` field. And/Or nodes aggregate: an And's satisfied bit is the
// logical AND of its children, an Or is the logical OR of its children.

struct TypeConstraintNode {
    enum class Kind {
        And,
        Or,
        Relation,
        Leaf,
    };

    Kind kind{Kind::Leaf};
    std::string relation;       // for Kind::Relation: "assignable", "equivalent", ...
    std::string path;           // e.g. "map.key", "list.element", "struct.field:x"
    std::string left_describe;  // textual left type
    std::string right_describe; // textual right type
    bool satisfied{false};
    std::vector<TypeConstraintNode> children;

    // Recompute `satisfied` from children for And/Or nodes.
    void aggregate_from_children() noexcept {
        if (kind == Kind::And) {
            satisfied = !children.empty();
            for (const auto &c : children) {
                satisfied = satisfied && c.satisfied;
            }
        } else if (kind == Kind::Or) {
            satisfied = false;
            for (const auto &c : children) {
                satisfied = satisfied || c.satisfied;
            }
        }
    }
};

struct TypeConstraintSkeleton {
    TypeConstraintNode root;

    [[nodiscard]] std::string to_string() const;
};

// ---------------------------------------------------------------------------
// TypeRelationOptions + TypeRelationContext
// ---------------------------------------------------------------------------

struct TypeRelationOptions {
    // When true, failed (and optionally success) relation steps are recorded as
    // flat TypeRelationTraceStep entries via the TypeRelationContext::trace()
    // accessor. Default: false — no flat trace is recorded.
    bool enable_trace{false};

    // When enable_trace is true, also record steps whose outcome is
    // "satisfied". Default: false — only failures are recorded to keep trace
    // volume low for diagnostics consumption.
    bool include_success_steps{false};

    // Maximum nesting depth for trace / skeleton node recording. Steps deeper
    // than this are still evaluated relationally but are dropped from trace /
    // skeleton. Guards against pathological deep nesting.
    int max_depth{64};

    // When true, relational checks populate a TypeConstraintSkeleton.
    // Default: false — no tree is built and no extra allocations occur.
    bool emit_constraint_skeleton{false};

    // When true, BoundedString(n..m) is treated as a subtype of plain String,
    // and a wider BoundedString(0..MAX) accepts a narrower one. Disabling this
    // is useful for "strict schema" mode where field type strings must match
    // exactly. Default: true — matches AHFL source-level semantics.
    bool allow_bounded_string_relaxation{true};

    // When true, numeric types follow widening rules: narrower numeric types
    // are assignable to wider ones (e.g. Int -> Float). Default: true.
    bool allow_numeric_widening{true};
};

// ---------------------------------------------------------------------------
// Flat relation trace (P1.1/P1.2/P1.3)
// ---------------------------------------------------------------------------
//
// When TypeRelationOptions::enable_trace is true, each relational check
// appends flat TypeRelationTraceStep records. This structure is designed for
// direct consumption by diagnostics (see format_trace_notes below).

enum class TypeRelationResult {
    Accepted,
    Rejected
};

enum class TypeRelationKind {
    Equivalent,
    Subtype,
    Assignable,
    ExactSchema
};

struct TypeRelationTraceStep {
    TypeRelationKind kind{TypeRelationKind::Assignable};
    int depth{0};
    std::string path;              // e.g. "list.element", "map.key"
    std::string expected_describe; // target type describe()
    std::string actual_describe;   // source type describe()
    TypeRelationResult result{TypeRelationResult::Rejected};
    std::string reason; // human readable note
};

struct RelationTrace {
    std::vector<TypeRelationTraceStep> steps;

    // Format the trace as up to `max_notes` human readable diagnostic strings.
    // Failed steps are surfaced first; if the caller asked for them, success
    // steps follow. Notes are prefixed with the path so they can be emitted as
    // diagnostics related notes.
    [[nodiscard]] std::vector<std::string> format_notes(std::size_t max_notes = 8) const;

    [[nodiscard]] bool empty() const noexcept {
        return steps.empty();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return steps.size();
    }
    void clear() noexcept {
        steps.clear();
    }
};

// ---- Forward declarations of 3-arg free function overloads -----------------
//
// These are forward-declared BEFORE TypeRelationContext so that the
// out-of-class member method definitions below (which share these names)
// can use ahfl::qualified lookup to unambiguously refer to the free
// function overload taking a TypeRelationContext &. Without these forward
// declarations, qualified lookup (ahfl::name) inside a member function
// definition would also match the member itself (name hiding) and error
// out on arity / const-ref mismatch.

class TypeRelationContext;

[[nodiscard]] bool are_types_equivalent(const Type &lhs, const Type &rhs, TypeRelationContext &ctx);
[[nodiscard]] bool is_subtype_of(const Type &source, const Type &target, TypeRelationContext &ctx);
[[nodiscard]] bool
is_assignable_to(const Type &source, const Type &target, TypeRelationContext &ctx);
[[nodiscard]] bool
is_exact_schema_match(const Type &source, const Type &target, TypeRelationContext &ctx);

// Holds per-call state shared by relation traversals. Intended as a stack
// object that is threaded down through recursive comparisons.
class TypeRelationContext {
  public:
    explicit TypeRelationContext(TypeRelationOptions opts = {}) : options_(opts) {}

    [[nodiscard]] const TypeRelationOptions &options() const noexcept {
        return options_;
    }

    // ---- Flat trace API ------------------------------------------------------

    // Legacy-compatible setter. Mirrors TypeRelationOptions::enable_trace.
    void enable_trace(bool v) noexcept {
        options_.enable_trace = v;
    }
    // When enable_trace is true, also record steps whose outcome is
    // "satisfied". Mirrors TypeRelationOptions::include_success_steps.
    void include_success_steps(bool v) noexcept {
        options_.include_success_steps = v;
    }
    [[nodiscard]] bool trace_enabled() const noexcept {
        return options_.enable_trace;
    }

    [[nodiscard]] const RelationTrace &trace() const noexcept {
        return flat_trace_;
    }
    [[nodiscard]] RelationTrace &trace() noexcept {
        return flat_trace_;
    }

    // Record a flat trace step if: (1) enable_trace is on, (2) depth < max_depth,
    // and (3) either the step failed or include_success_steps is set.
    void record_trace_step(TypeRelationKind kind,
                           int depth,
                           std::string path,
                           std::string expected_describe,
                           std::string actual_describe,
                           bool success,
                           std::string reason = {});

    // ---- Skeleton API --------------------------------------------------------

    // Access the built skeleton (only meaningful when
    // options().emit_constraint_skeleton == true).
    [[nodiscard]] const TypeConstraintSkeleton &skeleton() const noexcept {
        return constraint_skeleton_;
    }
    [[nodiscard]] TypeConstraintSkeleton &skeleton() noexcept {
        return constraint_skeleton_;
    }

    // Move the finished skeleton out of the context.
    [[nodiscard]] TypeConstraintSkeleton take_skeleton() noexcept {
        return std::move(constraint_skeleton_);
    }

    // ---- Node builder helpers ------------------------------------------------

    // Current stack of "open" nodes. push_* / pop_* keep this stack correct.
    // The root (index 0) is pushed once when the context is initialised for a
    // relation check; the outermost entry then points at skeleton.root.
    TypeConstraintNode *push_node(TypeConstraintNode node) {
        if (stack_.empty()) {
            constraint_skeleton_.root = std::move(node);
            stack_.push_back(&constraint_skeleton_.root);
            return stack_.back();
        }
        auto &parent = *stack_.back();
        parent.children.push_back(std::move(node));
        TypeConstraintNode *added = &parent.children.back();
        stack_.push_back(added);
        return added;
    }

    void pop_node() {
        if (!stack_.empty()) {
            stack_.back()->aggregate_from_children();
            stack_.pop_back();
        }
    }

    // Convenience builders.
    TypeConstraintNode *
    push_and(std::string path = {}, std::string left = {}, std::string right = {}) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::And;
        n.path = std::move(path);
        n.left_describe = std::move(left);
        n.right_describe = std::move(right);
        return push_node(std::move(n));
    }

    TypeConstraintNode *
    push_or(std::string path = {}, std::string left = {}, std::string right = {}) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::Or;
        n.path = std::move(path);
        n.left_describe = std::move(left);
        n.right_describe = std::move(right);
        return push_node(std::move(n));
    }

    TypeConstraintNode *push_relation(
        std::string rel, std::string path, std::string left, std::string right, bool satisfied) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::Relation;
        n.relation = std::move(rel);
        n.path = std::move(path);
        n.left_describe = std::move(left);
        n.right_describe = std::move(right);
        n.satisfied = satisfied;
        return push_node(std::move(n));
    }

    void add_leaf(std::string path, std::string left, std::string right, bool satisfied) {
        TypeConstraintNode n;
        n.kind = TypeConstraintNode::Kind::Leaf;
        n.path = std::move(path);
        n.left_describe = std::move(left);
        n.right_describe = std::move(right);
        n.satisfied = satisfied;
        push_node(std::move(n));
        pop_node(); // leaves have no children
    }

    [[nodiscard]] bool stack_empty() const noexcept {
        return stack_.empty();
    }

    // ---- Member-method shortcuts (mirror the 3-arg free functions) -----------
    //
    // Keeps call sites self-documenting: `ctx.equivalent(a, b)` instead of
    // `are_types_equivalent(a, b, ctx)`. Equivalent to the free-function
    // overloads; these are pure inline wrappers that delegate via qualified
    // lookup so the shared traversal code lives in one place.

    [[nodiscard]] bool equivalent(const Type &lhs, const Type &rhs) {
        return ahfl::are_types_equivalent(lhs, rhs, *this);
    }
    [[nodiscard]] bool subtype(const Type &source, const Type &target) {
        return ahfl::is_subtype_of(source, target, *this);
    }
    [[nodiscard]] bool assignable(const Type &source, const Type &target) {
        return ahfl::is_assignable_to(source, target, *this);
    }
    [[nodiscard]] bool exact_schema(const Type &source, const Type &target) {
        return ahfl::is_exact_schema_match(source, target, *this);
    }

  private:
    TypeRelationOptions options_{};
    RelationTrace flat_trace_{};
    TypeConstraintSkeleton constraint_skeleton_{};
    std::vector<TypeConstraintNode *> stack_;
};

// ---- TypeRelationContext inline method bodies -----------------------------
//
// Defined OUTSIDE the class body. 3-arg free-function overloads are
// ---------------------------------------------------------------------------
// Relation entry points with optional context.
// ---------------------------------------------------------------------------
//
// The context-free overloads are preserved for call sites that don't care
// about trace / diagnostics; they construct a temporary context with default
// (non-emitting) options and delegate.

[[nodiscard]] bool are_types_equivalent(const Type &lhs, const Type &rhs);
[[nodiscard]] bool are_types_equivalent(const Type &lhs, const Type &rhs, TypeRelationContext &ctx);

[[nodiscard]] bool is_subtype_of(const Type &source, const Type &target);
[[nodiscard]] bool is_subtype_of(const Type &source, const Type &target, TypeRelationContext &ctx);

[[nodiscard]] bool is_assignable_to(const Type &source, const Type &target);
[[nodiscard]] bool
is_assignable_to(const Type &source, const Type &target, TypeRelationContext &ctx);

[[nodiscard]] bool is_exact_schema_match(const Type &source, const Type &target);
[[nodiscard]] bool
is_exact_schema_match(const Type &source, const Type &target, TypeRelationContext &ctx);

enum class SchemaBoundaryKind {
    AgentInput,
    AgentOutput,
    AgentContextDefault,
    WorkflowInput,
    WorkflowOutput,
    WorkflowNodeInput,
};

[[nodiscard]] std::string_view to_string(SchemaBoundaryKind kind) noexcept;

[[nodiscard]] bool are_types_equivalent(const Type &lhs, const Type &rhs);
[[nodiscard]] bool is_subtype_of(const Type &source, const Type &target);
[[nodiscard]] bool is_assignable_to(const Type &source, const Type &target);
[[nodiscard]] bool is_exact_schema_match(const Type &source, const Type &target);

} // namespace ahfl

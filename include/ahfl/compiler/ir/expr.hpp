#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/node_tags.hpp"
#include "ahfl/compiler/ir/types.hpp"
#include "ahfl/compiler/semantics/effects.hpp"

namespace ahfl::ir {

// ----------------------------------------------------------------------------
// Path
// ----------------------------------------------------------------------------

/// Path expression (e.g. input.category, classify.confidence)
struct Path {
    PathRootKind root_kind{PathRootKind::Identifier};
    std::string root_name{};            // Root name ("input", "ctx", or an identifier name)
    std::vector<std::string> members{}; // Member access chain
};

// ----------------------------------------------------------------------------
// Expression Layer
// ----------------------------------------------------------------------------

struct Expr;
struct MatchPattern;

/// Stable handle for an arena-owned expression node.
///
/// The index is the durable identity used by Program::expr_arena; the cached
/// pointer keeps existing recursive IR algorithms cheap and avoids threading
/// arena lookups through every visitor/printer/evaluator call site.
struct ExprRef {
    using Index = std::uint32_t;
    static constexpr Index kInvalid = UINT32_MAX;

    Expr *ptr{nullptr};
    Index index{kInvalid};

    ExprRef() = default;
    ExprRef(Expr &expr, Index expr_index) noexcept : ptr(&expr), index(expr_index) {}
    ExprRef(std::nullptr_t) noexcept {}
    ExprRef &operator=(std::nullptr_t) noexcept {
        ptr = nullptr;
        index = kInvalid;
        return *this;
    }

    [[nodiscard]] bool has_value() const noexcept {
        return ptr != nullptr;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }
    [[nodiscard]] Expr *get() const noexcept {
        return ptr;
    }
    [[nodiscard]] Expr &operator*() const noexcept {
        assert(ptr != nullptr);
        return *ptr;
    }
    [[nodiscard]] Expr *operator->() const noexcept {
        assert(ptr != nullptr);
        return ptr;
    }

    [[nodiscard]] friend bool operator==(const ExprRef &ref, std::nullptr_t) noexcept {
        return ref.ptr == nullptr;
    }
    [[nodiscard]] friend bool operator==(std::nullptr_t, const ExprRef &ref) noexcept {
        return ref.ptr == nullptr;
    }
    [[nodiscard]] friend bool operator!=(const ExprRef &ref, std::nullptr_t) noexcept {
        return ref.ptr != nullptr;
    }
    [[nodiscard]] friend bool operator!=(std::nullptr_t, const ExprRef &ref) noexcept {
        return ref.ptr != nullptr;
    }
};

struct TemporalExpr;
using TemporalExprPtr = Owned<TemporalExpr>;

struct Statement;
using StatementPtr = Owned<Statement>;

using SourceRangeOpt = std::optional<SourceRange>;

// ----------------------------------------------------------------------------
// Pattern Layer
// ----------------------------------------------------------------------------

/// Literal pattern: true / false / integer / string / none.
struct LiteralPattern {
    std::string spelling;
};

/// Integer range pattern: start..end.
struct IntRangePattern {
    std::int64_t start{0};
    std::int64_t end{0};
};

/// Variant pattern: Result::Ok(value), Some(_), Err(error).
enum class VariantPatternKind {
    Unit,
    Tuple,
    Struct,
};

struct VariantPatternField {
    std::string name;
    Owned<MatchPattern> pattern;
    bool is_rest{false};
};

struct VariantPattern {
    std::string path; // display only; owner_enum + variant_name are identity
    VariantPatternKind kind{VariantPatternKind::Unit};
    std::vector<Owned<MatchPattern>> subpatterns;
    std::vector<VariantPatternField> fields;
    // Typed identity persisted from Sema's TypedPattern (RFC 0026 (3)-3b): the
    // owning enum's resolved symbol and the declaration-stable variant name.
    // A Core lowerer resolves the variant by symbol identity, NEVER by parsing
    // `path`. `owner_enum.kind == Unknown` means the fact was not resolved
    // (fail-closed downstream).
    SymbolRef owner_enum{};
    std::string variant_name{};
};

/// Wildcard pattern: _.
struct WildcardPattern {};

/// Binding pattern: name or name @ nested.
struct BindingPattern {
    std::string name;
    bool is_mut{false};
    Owned<MatchPattern> nested{};
};

/// Tuple pattern: (a, b, c).
struct TuplePattern {
    std::vector<Owned<MatchPattern>> elements;
};

/// Or pattern: a | b.
struct OrPattern {
    std::vector<Owned<MatchPattern>> branches;
};

namespace node_detail {

// Tag tuples for every X-macro-generated IR variant (KR6.13-F). Each is
// expanded from its .def list; the variant is reconstructed from the element
// types, so declaration order in the .def IS alternative order.
#define HANDLE_PATTERN_NODE(Name, Wire) node_tag<Name>{},
inline constexpr auto kPatternNodeTags = std::tuple{
#include "ahfl/compiler/ir/pattern_nodes.def"
};
#undef HANDLE_PATTERN_NODE

} // namespace node_detail

// RFC 0027 P6/P7/P8 (KR6.13-F/P7): the alternative list is generated from the
// single X-macro node list pattern_nodes.def — one line per node, declaration
// order preserved. To add a node, edit ONLY that .def; the variant, every
// exhaustive visitor, and (via the negative compile-test) the whole consumer
// set stay in lockstep, and a node that misses a handler is a compile error
// naming the type rather than a silently skipped case.
using MatchPatternNode = node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(node_detail::kPatternNodeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate. The count itself now
// derives from pattern_nodes.def; this pin turns "a node was added to the .def"
// into a deliberate review event across every exhaustive visitor.
static_assert(std::variant_size_v<MatchPatternNode> == 7,
              "ahfl::ir::MatchPatternNode cardinality drift (RFC 0027 P8 IR "
              "SSOT): update every exhaustive visitor and this pin together "
              "with pattern_nodes.def.");

namespace match_pattern_node_detail {

/// Ordered strong index of each MatchPatternNode alternative (RFC 0027 Q1
/// pattern: index-based identity, never strings).
enum class MatchPatternNodeIndex : std::size_t {
#define HANDLE_PATTERN_NODE(Name, Wire) Name,
#include "ahfl/compiler/ir/pattern_nodes.def"
};

/// JSON wire name of each MatchPatternNode alternative, indexed by
/// `MatchPatternNode::index()`. The ONE table the ir_json writer and reader
/// resolve the `"kind"` spelling from.
inline constexpr std::array<std::string_view,
                            std::tuple_size_v<std::remove_cvref_t<decltype(
                                node_detail::kPatternNodeTags)>>>
    kMatchPatternWireNames = {
#define HANDLE_PATTERN_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/pattern_nodes.def"
};

} // namespace match_pattern_node_detail

/// JSON wire name of the MatchPatternNode alternative at `variant_index` (a
/// `MatchPatternNode::index()` value, therefore always in bounds).
[[nodiscard]] inline constexpr std::string_view
match_pattern_node_wire_name(std::size_t variant_index) noexcept {
    return match_pattern_node_detail::kMatchPatternWireNames[variant_index];
}

/// JSON wire name of a match-pattern node (e.g. `"variant"`).
[[nodiscard]] inline std::string_view
match_pattern_node_wire_name(const MatchPatternNode &node) noexcept {
    return match_pattern_node_wire_name(node.index());
}

struct MatchPattern {
    MatchPatternNode node;
    SourceRangeOpt source_range;
    std::string text{};
    // The NOMINAL type this pattern is matched AGAINST, persisted from Sema's
    // TypedPattern.matched_type (RFC 0026 (3)-3b). Despite the historical name,
    // this resolves ANY nominal type — an enum OR a struct — via
    // matched_enum_ref() / TypeEnv::resolve(). It is needed because (a) a literal
    // pattern like `none` stays a LiteralPattern whose enum identity lives only
    // in the scrutinee's matched type, so (3)-3c can lower `none` to the
    // Option::None variant pattern by symbol identity (never by spelling); and
    // (b) a payload BINDING carries the INSTANTIATED nominal payload type here
    // (e.g. `Some(u)` on `Option<User>` records `User`), which the Core lowerer
    // uses so `u.field` resolves even though the builtin generic Option's payload
    // slot type is an unresolved type-parameter placeholder.
    // `matched_enum.kind == Unknown` means the pattern is not matched against a
    // resolved nominal type (e.g. an integer/bool scrutinee), which is fine.
    // TODO(rename): call this matched_nominal once the cross-file rename is worth
    // its own churn-only commit; the semantics are "matched nominal type", not
    // "enum".
    SymbolRef matched_enum{};
    // RFC 0026 P4-B: the FULL resolved type this pattern node is matched against,
    // persisted from Sema's TypedPattern.matched_type. Unlike `matched_enum`
    // (nominal-only SymbolRef), this carries ANY type — primitive / bounded /
    // Fn / nominal-generic — so a payload binding like `Some(v)` on `Option<Int>`
    // recovers `Int` (and its bounds), which the nominal-only symbol cannot
    // express. Each MatchPattern node is 1:1 with a Sema TypedPattern, so this is
    // that node's exact matched type. `kind == Unresolved` when Sema had no
    // resolved type for the node (e.g. a wildcard against an unresolved scrutinee);
    // a Core consumer that needs the type fails closed on Unresolved rather than
    // guessing. BackendReady invariant: when this is a nominal (Struct/Enum) type
    // its `nominal_ref` agrees with `matched_enum`; when it is a primitive,
    // `matched_enum` is Unknown.
    TypeRef matched_type_ref{};
};

/// Boolean literal: true / false
struct BoolLiteralExpr {
    bool value{false};
};

/// Integer literal: 42, -1
struct IntegerLiteralExpr {
    std::string spelling; // Original text (kept for diagnostics)
};

/// Float literal: 3.14
struct FloatLiteralExpr {
    std::string spelling;
};

/// Fixed-point decimal literal: 3.14d
struct DecimalLiteralExpr {
    std::string spelling;
};

/// String literal: "hello"
struct StringLiteralExpr {
    std::string spelling;
};

/// Duration literal: 30s, 5m
struct DurationLiteralExpr {
    std::string spelling;
};

/// Path expression: input.field, ctx.field, node_name.field
struct PathExpr {
    Path path;
};

/// Qualified value expression: Priority::High
struct QualifiedValueExpr {
    std::string value; // Fully qualified name
};

/// Function call expression: capability_name(arg1, arg2, ...)
struct CallExpr {
    std::string callee;             // Callee name (capability name)
    std::vector<ExprRef> arguments; // Argument list
    SymbolRef callee_ref{};         // Resolved identity; strings are display/diagnostic only
};

/// Method call expression: receiver.method(arg1, arg2, ...).
///
/// KR5.5 / RFC 0013 P3: a first-class node that preserves the
/// receiver/method distinction across the IR boundary, instead of flattening
/// the receiver into the first positional argument of a CallExpr. `method`
/// holds the resolved dispatch target string (a `impl#<index>::<name>` handle
/// or a `@builtin` hook name — the same string a CallExpr callee carried
/// before this node existed), so the runtime evaluator can dispatch it exactly
/// like the equivalent free call. `method_ref` is the resolved impl/trait
/// method symbol; strings are display/diagnostic only (Principle 2).
struct MethodCallExpr {
    ExprRef receiver;               // Receiver object (the `self` argument)
    std::string method;             // Resolved dispatch target (impl#N::name / builtin)
    std::vector<ExprRef> arguments; // Explicit argument list (excludes the receiver)
    SymbolRef method_ref{};         // Resolved identity; strings are display/diagnostic only
};

/// Pure lambda expression lowered from a typed closure.
struct LambdaExpr {
    std::vector<std::string> params;
    ExprRef body;
    // C-4 (Wave-24): explicit capture list; empty = implicit-capture form.
    // Placed after `body` so the pre-C-4 aggregate-initialization pattern
    // LambdaExpr{params, body} remains well-formed (trailing fields are
    // default-initialized to empty).
    std::vector<std::string> captures;
};

/// Struct field initializer
struct StructFieldInit {
    std::string name; // Field name
    ExprRef value;    // Initializer expression
};

/// Struct literal: TypeName { field1: val1, field2: val2 }
struct StructLiteralExpr {
    std::string type_name;               // Type name
    std::vector<StructFieldInit> fields; // Field initializer list
    bool is_enum_variant{false};
    std::string enum_name{};
    std::string variant_name{};
};

/// Unary expression: !expr, -expr
struct UnaryExpr {
    ExprUnaryOp op{ExprUnaryOp::Not};
    ExprRef operand;
};

/// Binary expression: a + b, a == b
struct BinaryExpr {
    ExprBinaryOp op{ExprBinaryOp::Implies};
    ExprRef lhs;
    ExprRef rhs;
};

/// Member access expression: expr.member
struct MemberAccessExpr {
    ExprRef base;       // Base object
    std::string member; // Member name
};

/// Index access expression: expr[index]
struct IndexAccessExpr {
    ExprRef base;  // Base object
    ExprRef index; // Index expression
};

/// A single match expression arm.
struct MatchArmExpr {
    MatchPattern pattern;
    ExprRef guard;
    ExprRef body;
};

/// Match expression: match scrutinee { pattern [if guard] => body, ... }.
struct MatchExpr {
    ExprRef scrutinee;
    std::vector<MatchArmExpr> arms;
};

/// P4-02: unwrap(operand) as a right-hand-side expression.  Produces T from
/// an Option<T>-typed operand at runtime; when the operand is the None
/// variant the evaluator raises ExecAssertFailed with the message stored in
/// `fallback_none_message` (or the built-in default when null).
struct UnwrapExpr {
    ExprRef operand;
    ExprRef fallback_none_message{nullptr}; // user-provided failure message (rare)
};

/// RFC 0013 P3-gaps-B: the unit literal `{}`. The sole value of the `Unit`
/// type. Zero-sized; carries no data.
struct UnitLiteralExpr {};

/// RFC 0024: bounded collection quantifier `forall x in coll: body` /
/// `exists (k, v) in coll: body`. A verification-only predicate: the SMT-BMC
/// backend unrolls it over the collection's static bound, substituting the
/// binder(s) with per-index element symbols. The runtime evaluator never
/// executes it (quantifiers appear only in contract clauses / pure predicate
/// position). Binders are name-based, mirroring LambdaExpr — the body
/// references them through PathExpr, so no new value-identity concept is
/// introduced.
struct QuantifierExpr {
    enum class Kind : std::uint8_t { ForAll, Exists };
    Kind kind{Kind::ForAll};
    std::string binder;       // element binder (List/Set) or key binder (Map)
    std::string value_binder; // Map value binder; empty for List/Set
    ExprRef collection;       // the quantified collection operand
    ExprRef body;             // the Bool-typed body predicate
};

/// Expression node (20 variant alternatives - P5 Big Bang: container literals
/// lowered to CallExpr via nominal stdlib constructors, Option variants via
/// QualifiedValueExpr + CallExpr; KR5.5 added MethodCallExpr)
///
/// RFC 0027 P6/P7 (KR6.13-E): the alternative list AND its JSON wire names are
/// generated from the single X-macro node list in expr_nodes.def — one line per
/// node, declaration order preserved. To add a node, edit ONLY that .def; the
/// variant, the internal `ExprNodeIndex` enum, the wire-name table, and (via
/// the negative compile-test) every exhaustive visitor stay in lockstep. The
/// SWEEP CHECKLIST that used to live here covered the eight hand-written
/// consumer sites for ExprNode; those sites still route through std::visit and
/// are now enumerated by the .def + this generated variant, so a new node that
/// misses one is a compile error rather than a silent data-loss bug. The
/// remaining IR families (Statement / TemporalExpr / MatchPattern / Decl / …)
/// keep their own checklists until later KR6.13 slices migrate them.
///
/// The tuple-tag indirection lives in node_tags.hpp (KR6.13-F), shared by every
/// X-macro-generated IR variant.
namespace expr_node_detail {

// The edge column (third .def argument, KR6.13-T) is unused by the variant and
// wire-table derivations below; the child-edge traversal derives from it in
// ahfl/compiler/ir/expr_child_edges.hpp.
#define HANDLE_EXPR_NODE(Name, Wire, Edges) node_detail::node_tag<Name>{},
inline constexpr auto kExprNodeTags = std::tuple{
#include "ahfl/compiler/ir/expr_nodes.def"
};

} // namespace expr_node_detail

using ExprNode = node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(expr_node_detail::kExprNodeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see MatchPatternNode). The
// count itself now derives from expr_nodes.def; this pin turns "a node was added
// to the .def" into a deliberate review event across every exhaustive visitor.
static_assert(std::variant_size_v<ExprNode> == 20,
              "ahfl::ir::ExprNode cardinality drift (RFC 0027 P8 IR SSOT): "
              "update every exhaustive visitor, this pin, and the ExprNode "
              "entry list (expr_nodes.def) together.");

namespace expr_node_detail {

/// Ordered strong index of each ExprNode alternative (RFC 0027 Q1 pattern:
/// index-based identity, never strings). Value equals the alternative's
/// `ExprNode::index()`; extension points that need named positions use this
/// rather than a magic literal.
enum class ExprNodeIndex : std::size_t {
#define HANDLE_EXPR_NODE(Name, Wire, Edges) Name,
#include "ahfl/compiler/ir/expr_nodes.def"
};

/// JSON wire name of each ExprNode alternative, indexed by
/// `ExprNode::index()` (equivalently `ExprNodeIndex`). This is the ONE table
/// both the ir_json writer and reader resolve the `"kind"` spelling from, so
/// the two sides can never silently diverge on a wire name.
inline constexpr std::array<std::string_view,
                            std::tuple_size_v<std::remove_cvref_t<decltype(kExprNodeTags)>>>
    kExprNodeWireNames = {
#define HANDLE_EXPR_NODE(Name, Wire, Edges) Wire,
#include "ahfl/compiler/ir/expr_nodes.def"
};

} // namespace expr_node_detail

/// JSON wire name of the ExprNode alternative at `variant_index` (a
/// `ExprNode::index()` value, therefore always in bounds).
[[nodiscard]] inline constexpr std::string_view
expr_node_wire_name(std::size_t variant_index) noexcept {
    return expr_node_detail::kExprNodeWireNames[variant_index];
}

/// JSON wire name of an expression node (e.g. `"bool_literal"`).
[[nodiscard]] inline std::string_view expr_node_wire_name(const ExprNode &node) noexcept {
    return expr_node_wire_name(node.index());
}

/// Expression wrapper struct
struct Expr {
    ExprNode node;
    SourceRangeOpt source_range;
    TypeRef resolved_type; // Populated during lowering; kind=Unresolved if unavailable
    ExprEffect effect{ExprEffect::Unknown}; // Inferred by Sema; backend/formal input
    std::uint32_t id{0};   // Monotonic node ID assigned during lowering (E-2)
};

// ----------------------------------------------------------------------------
// Temporal Expression Layer
// ----------------------------------------------------------------------------

/// Embedded ordinary expression
struct EmbeddedTemporalExpr {
    ExprRef expr;
};

/// called(capability_name) — a capability has been called
struct CalledTemporalExpr {
    std::string capability;
};

/// in_state(agent, state) — an agent is in a given state
struct InStateTemporalExpr {
    std::string state;
};

/// running(agent) — an agent is currently running
struct RunningTemporalExpr {
    std::string node;
};

/// completed(agent) — an agent has completed
struct CompletedTemporalExpr {
    std::string node;
    std::optional<std::string> state_name; // Optional terminal state
};

/// Temporal unary expression: always(expr), eventually(expr)
struct TemporalUnaryExpr {
    TemporalUnaryOp op{TemporalUnaryOp::Always};
    TemporalExprPtr operand;
};

/// Temporal binary expression: a U b (a until b)
struct TemporalBinaryExpr {
    TemporalBinaryOp op{TemporalBinaryOp::Implies};
    TemporalExprPtr lhs;
    TemporalExprPtr rhs;
};

namespace node_detail {

#define HANDLE_TEMPORAL_NODE(Name, Wire) node_tag<Name>{},
inline constexpr auto kTemporalNodeTags = std::tuple{
#include "ahfl/compiler/ir/temporal_nodes.def"
};
#undef HANDLE_TEMPORAL_NODE

} // namespace node_detail

/// Temporal expression node — generated from temporal_nodes.def (KR6.13-F).
using TemporalExprNode = node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(node_detail::kTemporalNodeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see MatchPatternNode).
static_assert(std::variant_size_v<TemporalExprNode> == 7,
              "ahfl::ir::TemporalExprNode cardinality drift (RFC 0027 P8 IR "
              "SSOT): update every exhaustive visitor and this pin together "
              "with temporal_nodes.def.");

namespace temporal_node_detail {

/// Ordered strong index of each TemporalExprNode alternative (RFC 0027 Q1
/// pattern: index-based identity, never strings).
enum class TemporalExprNodeIndex : std::size_t {
#define HANDLE_TEMPORAL_NODE(Name, Wire) Name,
#include "ahfl/compiler/ir/temporal_nodes.def"
};

/// JSON wire name of each TemporalExprNode alternative, indexed by
/// `TemporalExprNode::index()`. The ONE table the ir_json writer and reader
/// resolve the `"kind"` spelling from.
inline constexpr std::array<std::string_view,
                            std::tuple_size_v<std::remove_cvref_t<decltype(
                                node_detail::kTemporalNodeTags)>>>
    kTemporalWireNames = {
#define HANDLE_TEMPORAL_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/temporal_nodes.def"
};

} // namespace temporal_node_detail

/// JSON wire name of the TemporalExprNode alternative at `variant_index` (a
/// `TemporalExprNode::index()` value, therefore always in bounds).
[[nodiscard]] inline constexpr std::string_view
temporal_node_wire_name(std::size_t variant_index) noexcept {
    return temporal_node_detail::kTemporalWireNames[variant_index];
}

/// JSON wire name of a temporal node (e.g. `"embedded_expr"`).
[[nodiscard]] inline std::string_view
temporal_node_wire_name(const TemporalExprNode &node) noexcept {
    return temporal_node_wire_name(node.index());
}

/// Temporal expression wrapper struct
struct TemporalExpr {
    TemporalExprNode node;
    SourceRangeOpt source_range;
};

// ----------------------------------------------------------------------------
// Statement Layer
// ----------------------------------------------------------------------------

/// Statement block: { stmt1; stmt2; ... }
struct Block {
    std::vector<StatementPtr> statements;
    SourceRangeOpt source_range;
};

/// RFC 0026 P4 (coercion): AHFL-IR mirror of the Sema `TypedAdjustmentPlan`.
/// Carries the canonical compositional type-adjustment witness for an annotated
/// `let x: T = e` (A <: T) across the Typed HIR -> AHFL-IR boundary so the Core
/// layer never re-derives subtyping. Types are `TypeRef` (with nominal_ref
/// identity); the node arena is flat and index-referenced. See
/// `ahfl::TypedAdjustmentPlan` for the full contract.
enum class AdjustmentOpKind {
    IntWiden,
    StringWiden,
    CapacityWiden,
    TypeArg,
    FnParam,
    FnReturn,
    VariantToEnum,
    ToAny,
    FromNever,
};

struct AdjustmentOp {
    AdjustmentOpKind kind{AdjustmentOpKind::IntWiden};
    std::uint32_t arg_index{0};       // TypeArg / FnParam projection position
    std::uint32_t child{0xFFFFFFFFu}; // index into AdjustmentPlan::nodes; 0xFFFFFFFF = leaf
};

struct AdjustmentNode {
    TypeRef source;
    TypeRef target;
    std::vector<AdjustmentOp> ops;
};

struct AdjustmentPlan {
    TypeRef source;
    TypeRef target;
    std::vector<AdjustmentNode> nodes; // flat arena, index-referenced
    std::uint32_t root{0};             // index into `nodes` of the root node
};

/// let binding statement: let name: Type = initializer;
struct LetStatement {
    std::string name;    // Variable name
    TypeRef type_ref;    // Bound type
    ExprRef initializer; // Initializer expression
    // RFC 0026 P4 (coercion): canonical adjustment plan when the declared type
    // differs from the initializer's actual type (A <: T). nullopt for an exact
    // / inferred let (boundary types identical).
    std::optional<AdjustmentPlan> adjustment;
};

/// Assignment statement: target = value;
struct AssignStatement {
    Path target;   // Assignment target path
    ExprRef value; // Assignment expression
};

/// Conditional statement: if (condition) { then } else { else }
struct IfStatement {
    ExprRef condition;       // Condition expression
    Owned<Block> then_block; // then branch
    Owned<Block> else_block; // else branch (optional)
};

/// Pattern conditional statement: if let Pattern = scrutinee { then } else { else }
struct IfLetStatement {
    MatchPattern pattern;
    ExprRef scrutinee;
    Owned<Block> then_block;
    Owned<Block> else_block;
};

/// State jump statement: goto StateName;
struct GotoStatement {
    std::string target_state; // Target state name
};

/// Return statement: return expr;
struct ReturnStatement {
    ExprRef value; // Return value expression
};

/// Assert statement: assert(condition[, "message"]);
struct AssertStatement {
    ExprRef condition;          // Assertion condition (Bool)
    ExprRef message{nullptr};   // Optional user-facing failure message (String)
};

/// Unwrap statement: unwrap(operand);
///
/// P4-01 semantics: assert that the operand is Some(_). Value extraction
/// (`let x = unwrap(opt)`) is deferred to a follow-up P4-02 `unwrap_expr`.
struct UnwrapStatement {
    ExprRef operand; // Expression of type Optional<T>
};

/// Requires statement: requires(condition[, "message"]);
///
/// Mirrors AssertStatement at the IR level; a distinct struct is kept so that
/// backends (CLI failure reports, LSP diagnostics, formal verifiers) can
/// present "contract violation" vs. "internal assert failure" differently.
struct RequiresStatement {
    ExprRef condition;          // Bool guard
    ExprRef message{nullptr};   // Optional failure message
};

/// Unreachable statement: unreachable[("message")];
///
/// A hard dynamic failure: evaluator throws ExecAssertFailed whenever this
/// statement is executed. Static reachability proofs are a separate pass.
struct UnreachableStatement {
    ExprRef message{nullptr}; // Optional failure message
};

/// Expression statement (e.g. capability call): expr;
struct ExprStatement {
    ExprRef expr;
};

namespace node_detail {

#define HANDLE_STMT_NODE(Name, Wire) node_tag<Name>{},
inline constexpr auto kStmtNodeTags = std::tuple{
#include "ahfl/compiler/ir/stmt_nodes.def"
};
#undef HANDLE_STMT_NODE

} // namespace node_detail

/// Statement node — generated from stmt_nodes.def (KR6.13-F).
using StatementNode =
    node_detail::variant_from_tags_t<std::remove_cvref_t<decltype(node_detail::kStmtNodeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see MatchPatternNode).
static_assert(std::variant_size_v<StatementNode> == 11,
              "ahfl::ir::StatementNode cardinality drift (RFC 0027 P8 IR "
              "SSOT): update every exhaustive visitor and this pin together "
              "with stmt_nodes.def.");

namespace stmt_node_detail {

/// Ordered strong index of each StatementNode alternative (RFC 0027 Q1 pattern:
/// index-based identity, never strings).
enum class StmtNodeIndex : std::size_t {
#define HANDLE_STMT_NODE(Name, Wire) Name,
#include "ahfl/compiler/ir/stmt_nodes.def"
};

/// JSON wire name of each StatementNode alternative, indexed by
/// `StatementNode::index()`. The ONE table the ir_json writer and reader resolve
/// the `"kind"` spelling from.
inline constexpr std::array<std::string_view,
                            std::tuple_size_v<std::remove_cvref_t<decltype(
                                node_detail::kStmtNodeTags)>>>
    kStmtWireNames = {
#define HANDLE_STMT_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/stmt_nodes.def"
};

} // namespace stmt_node_detail

/// JSON wire name of the StatementNode alternative at `variant_index` (a
/// `StatementNode::index()` value, therefore always in bounds).
[[nodiscard]] inline constexpr std::string_view
stmt_node_wire_name(std::size_t variant_index) noexcept {
    return stmt_node_detail::kStmtWireNames[variant_index];
}

/// JSON wire name of a statement node (e.g. `"if_let"`).
[[nodiscard]] inline std::string_view stmt_node_wire_name(const StatementNode &node) noexcept {
    return stmt_node_wire_name(node.index());
}

/// Statement wrapper struct
struct Statement {
    StatementNode node;
    SourceRangeOpt source_range;
    std::uint32_t id{0}; // Monotonic statement ID assigned during lowering
};

} // namespace ahfl::ir

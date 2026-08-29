#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
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

using MatchPatternNode = std::variant<LiteralPattern,
                                      IntRangePattern,
                                      VariantPattern,
                                      WildcardPattern,
                                      BindingPattern,
                                      TuplePattern,
                                      OrPattern>;

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
/// ---------------------------------------------------------------------------
/// SWEEP CHECKLIST — every new ExprNode alternative MUST update all 8 locations
/// ---------------------------------------------------------------------------
/// Whenever you add a variant to `ExprNode`, grep each file below for the
/// pattern "case ExprKind::" / ".emplace<NewAlternative>" / the last similar
/// alternative and add the matching branch.  Missing any one of these produces
/// a silent data-loss bug (default branches tend to skip the new node).
///
///   1. src/compiler/ir/analysis.cpp            – IR traversals / cost models
///   2. src/compiler/ir/ir_print.cpp            – textual IR dumper
///   3. src/compiler/ir/verify.cpp              – BackendReady structural verifier
///   4. src/compiler/ir/ir_json.cpp             – JSON (de)serialization for IR
///   5. src/compiler/ir/opt/opt_lower.cpp       – optimisation / simplification
///   6. src/compiler/ir/visitor.cpp             – both const AND mutating visitors
///   7. src/compiler/ir/typed_hir_lower.cpp     – Typed HIR → IR construction
///   8. src/compiler/assurance/assurance.cpp    – assurance-probe IR walk
///
/// Wave-18 P4-02 baseline: this checklist was derived from a full repository
/// sweep after introducing UnwrapExpr; treat the list as authoritative.
using ExprNode = std::variant<BoolLiteralExpr,
                              IntegerLiteralExpr,
                              FloatLiteralExpr,
                              DecimalLiteralExpr,
                              StringLiteralExpr,
                              DurationLiteralExpr,
                              PathExpr,
                              QualifiedValueExpr,
                              CallExpr,
                              MethodCallExpr,
                              LambdaExpr,
                              StructLiteralExpr,
                              UnaryExpr,
                              BinaryExpr,
                              MemberAccessExpr,
                              IndexAccessExpr,
                              MatchExpr,
                              UnwrapExpr,
                              UnitLiteralExpr,
                              QuantifierExpr>;

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

/// Temporal expression node (7 variant alternatives)
using TemporalExprNode = std::variant<EmbeddedTemporalExpr,
                                      CalledTemporalExpr,
                                      InStateTemporalExpr,
                                      RunningTemporalExpr,
                                      CompletedTemporalExpr,
                                      TemporalUnaryExpr,
                                      TemporalBinaryExpr>;

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

/// let binding statement: let name: Type = initializer;
struct LetStatement {
    std::string name;    // Variable name
    TypeRef type_ref;    // Bound type
    ExprRef initializer; // Initializer expression
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

/// Statement node (12 variant alternatives)
using StatementNode = std::variant<LetStatement,
                                   AssignStatement,
                                   IfStatement,
                                   IfLetStatement,
                                   GotoStatement,
                                   ReturnStatement,
                                   AssertStatement,
                                   UnwrapStatement,
                                   RequiresStatement,
                                   UnreachableStatement,
                                   ExprStatement>;

/// Statement wrapper struct
struct Statement {
    StatementNode node;
    SourceRangeOpt source_range;
    std::uint32_t id{0}; // Monotonic statement ID assigned during lowering
};

} // namespace ahfl::ir

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

/// Variant pattern: Result::Ok(value), Some(_), Err(error).
struct VariantPattern {
    std::string path;
    std::vector<Owned<MatchPattern>> subpatterns;
};

/// Wildcard pattern: _.
struct WildcardPattern {};

/// Binding pattern: name or name @ nested.
struct BindingPattern {
    std::string name;
    bool is_mut{false};
    Owned<MatchPattern> nested;
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
                                      VariantPattern,
                                      WildcardPattern,
                                      BindingPattern,
                                      TuplePattern,
                                      OrPattern>;

struct MatchPattern {
    MatchPatternNode node;
    SourceRangeOpt source_range;
    std::string text;
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

/// Expression node (17 variant alternatives - P5 Big Bang: container literals
/// lowered to CallExpr via nominal stdlib constructors, Option variants via
/// QualifiedValueExpr + CallExpr)
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
                              LambdaExpr,
                              StructLiteralExpr,
                              UnaryExpr,
                              BinaryExpr,
                              MemberAccessExpr,
                              IndexAccessExpr,
                              MatchExpr,
                              UnwrapExpr>;

/// Expression wrapper struct
struct Expr {
    ExprNode node;
    SourceRangeOpt source_range;
    TypeRef resolved_type; // Populated during lowering; kind=Unresolved if unavailable
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

/// Statement node (11 variant alternatives)
using StatementNode = std::variant<LetStatement,
                                   AssignStatement,
                                   IfStatement,
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

#pragma once

#include <functional>
#include <string>
#include <variant>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Execution Outcome - control-flow result after a statement executes
// ============================================================================

struct ExecContinue {};

struct ExecGoto {
    std::string target_state;
};

struct ExecReturn {
    Value value;
};

// ============================================================================
// AssertionKind - classifier for every failure shape unified under ExecAssertFailed
// ============================================================================

enum class AssertionKind {
    /// `assert(cond, ...)` — cond evaluated to false.
    ASSERT_CLAUSE,
    /// `requires(cond, ...)` — contract precondition violated at runtime.
    REQUIRES_VIOLATION,
    /// Statement-level `unwrap(opt)` or expression-level `unwrap(expr)` where
    /// the operand carried the None / empty variant.
    UNWRAP_NONE,
    /// `unreachable;` or `unreachable(msg);` executed at runtime.
    UNREACHABLE_EXECUTED,
};

/// Human-readable name of an AssertionKind (UPPER_SNAKE — matches JSON/CLI output).
[[nodiscard]] constexpr std::string_view to_string(AssertionKind k) noexcept {
    using K = AssertionKind;
    switch (k) {
        case K::ASSERT_CLAUSE: return "ASSERT_CLAUSE";
        case K::REQUIRES_VIOLATION: return "REQUIRES_VIOLATION";
        case K::UNWRAP_NONE: return "UNWRAP_NONE";
        case K::UNREACHABLE_EXECUTED: return "UNREACHABLE_EXECUTED";
    }
    return "ASSERTION_KIND_UNKNOWN";
}

struct ExecAssertFailed {
    AssertionKind kind{AssertionKind::ASSERT_CLAUSE};
    std::string message;
};

using ExecOutcome = std::variant<ExecContinue, ExecGoto, ExecReturn, ExecAssertFailed>;

// ============================================================================
// ExecResult - execution result (control flow + diagnostics)
// ============================================================================

struct ExecResult {
    ExecOutcome outcome;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const {
        return diagnostics.has_error();
    }
};

// ============================================================================
// ExecContext - execution context (wraps EvalContext and provides mutable operations)
// ============================================================================

// Expression evaluation function type (supports optional capability calls)
using ExprEvalFn = std::function<EvalResult(const ir::Expr &, const EvalContext &)>;

struct ExecContext {
    EvalContext eval_ctx;
    // Optional custom expression evaluator (used to support CallExpr)
    ExprEvalFn expr_eval;

    void bind_local(const std::string &name, Value value);
    bool assign_ctx(const std::string &name, Value value);
    // Evaluate an expression: use the custom evaluator (if any) or the default eval_expr
    EvalResult eval_expression(const ir::Expr &expr) const;
};

// ============================================================================
// Core Execution Functions
// ============================================================================

[[nodiscard]] ExecResult exec_statement(const ir::Statement &stmt, ExecContext &ctx);
[[nodiscard]] ExecResult exec_block(const ir::Block &block, ExecContext &ctx);

} // namespace ahfl::evaluator

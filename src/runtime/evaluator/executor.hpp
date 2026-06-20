#pragma once

#include <functional>
#include <string>
#include <variant>

#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/base/support/diagnostics.hpp"

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

struct ExecAssertFailed {
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

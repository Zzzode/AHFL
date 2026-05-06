#pragma once

#include <functional>
#include <string>
#include <variant>

#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/evaluator.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Execution Outcome - 语句执行后的控制流结果
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
// ExecResult - 执行结果（控制流 + 诊断信息）
// ============================================================================

struct ExecResult {
    ExecOutcome outcome;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const {
        return diagnostics.has_error();
    }
};

// ============================================================================
// ExecContext - 执行上下文（包裹 EvalContext 并提供可变操作）
// ============================================================================

// 表达式求值函数类型（支持可选的 capability 调用）
using ExprEvalFn = std::function<EvalResult(const ir::Expr &, const EvalContext &)>;

struct ExecContext {
    EvalContext eval_ctx;
    // 可选的自定义表达式求值器（用于支持 CallExpr）
    ExprEvalFn expr_eval;

    void bind_local(const std::string &name, Value value);
    bool assign_ctx(const std::string &name, Value value);
    // 求值表达式：使用自定义求值器（如有）或默认的 eval_expr
    EvalResult eval_expression(const ir::Expr &expr) const;
};

// ============================================================================
// Core Execution Functions
// ============================================================================

[[nodiscard]] ExecResult exec_statement(const ir::Statement &stmt, ExecContext &ctx);
[[nodiscard]] ExecResult exec_block(const ir::Block &block, ExecContext &ctx);

} // namespace ahfl::evaluator

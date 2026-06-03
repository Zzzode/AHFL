#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/value.hpp"

#include <functional>

namespace ahfl::evaluator {

// ============================================================================
// Evaluation Result
// ============================================================================

struct EvalResult {
    Value value;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// ============================================================================
// Expression Evaluator
// ============================================================================

using CallEvalFn = std::function<EvalResult(const ir::CallExpr &, const EvalContext &)>;

[[nodiscard]] EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx);
[[nodiscard]] EvalResult
eval_expr(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn &call_eval);

} // namespace ahfl::evaluator

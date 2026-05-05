#pragma once

#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/support/diagnostics.hpp"

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

[[nodiscard]] EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx);

} // namespace ahfl::evaluator

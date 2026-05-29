#pragma once

#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/value.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/base/support/diagnostics.hpp"

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

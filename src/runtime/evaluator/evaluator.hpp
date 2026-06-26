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

/// Invoke a stdlib-namespaced intrinsic (``std::option::Option::*``,
/// ``std::collections::*``, builtins, etc.) with pre-evaluated arguments.
/// Used by the capability / program dispatch layers so that arguments can
/// be resolved via their custom call evaluators without the intrinsic path
/// re-entering expression evaluation for the same IR sub-trees.
[[nodiscard]] EvalResult eval_intrinsic_with_args(const std::string &callee,
                                                  std::vector<Value> args,
                                                  const EvalContext &ctx);

/// Create a call evaluator that can execute top-level IR function bodies from
/// a lowered program. Unknown calls fall back to constructor and builtin
/// dispatch, matching the default evaluator behavior.
[[nodiscard]] CallEvalFn make_program_call_eval(const ir::Program &program);

} // namespace ahfl::evaluator

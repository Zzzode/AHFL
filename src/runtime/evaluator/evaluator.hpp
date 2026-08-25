#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace ahfl::evaluator {

// ============================================================================
// Evaluation Result
// ============================================================================

// RFC 0022 (durable resume): a capability call deep in expression evaluation
// returned AHFL_CAP_PENDING. Evaluation unwinds to the workflow node loop
// carrying this record instead of a value or an error diagnostic. It is a third
// control-flow axis orthogonal to value/diagnostics — a suspended EvalResult has
// no meaningful value and no error. Identity is index/id-based (Principle 2):
// cap_id is a capability SymbolId, ordinal a per-node invocation ordinal.
struct EvalSuspension {
    std::size_t pending_cap_id{0};   // capability SymbolId of the pending call
    std::uint64_t pending_ordinal{0}; // per-node invocation ordinal of that call
};

struct EvalResult {
    Value value;
    DiagnosticBag diagnostics;
    // Set iff a capability call suspended (AHFL_CAP_PENDING). Default nullopt, so
    // every existing EvalResult{value, diags} construction is unchanged.
    std::optional<EvalSuspension> suspension{};

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }

    // True iff evaluation suspended on a pending capability call. Propagation
    // sites unwind on `has_errors() || is_suspended()` so a suspension is never
    // mistaken for a value (which would run further effects) or an error (which
    // would terminate the workflow).
    [[nodiscard]] bool is_suspended() const noexcept {
        return suspension.has_value();
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

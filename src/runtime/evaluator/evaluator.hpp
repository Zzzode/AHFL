#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/value/value.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

// ============================================================================
// First-class closure descriptor (interpreter-only)
// ============================================================================

// The tree-walking interpreter's closure representation. `runtime::Value`
// carries it inside an opaque `InterpreterClosureHandle` (see
// src/runtime/value/value.hpp): the host wire layer only forward-declares the
// descriptor, so nothing there depends on evaluator machinery. The definition
// is completed HERE because every member is interpreter state:
//
//   * `params` / `body`      -- the lambda in the IR the interpreter walks;
//   * `captured_context`     -- the interpreter environment snapshot taken at
//                               construction time (by value, so a closure
//                               captures its environment, not a live binding).
//
// It lives in `ahfl::runtime` because that is the namespace of the handle that
// owns it and of every other Value payload; the evaluator is the only layer
// that constructs or unwraps it. RFC 0026 KR6.8 retires this struct together
// with the evaluator, at which point the wire handle disappears from
// `ValueNode` as well.
//
// Storage is shared and immutable: one `make_shared<const InterpreterClosure>`
// is created at lambda evaluation, `clone_value` copies the handle rather than
// the captured environment, and every invocation reads through it. The
// shared_ptr is lifetime management ONLY: the closure's canonical identity is
// the monotonic `InterpreterClosureHandle::id` assigned by
// `make_interpreter_closure` (Principle 2), never the pointer value.
namespace ahfl::runtime {

struct InterpreterClosure {
    std::vector<std::string> params;
    const ir::Expr *body{nullptr};
    ahfl::evaluator::EvalContext captured_context;
};

} // namespace ahfl::runtime

namespace ahfl::evaluator {

using ahfl::runtime::InterpreterClosureRef;
using ahfl::runtime::make_interpreter_closure;
using ahfl::runtime::Value;

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
    // sites unwind on `should_unwind()` so a suspension is never mistaken for a
    // value (which would run further effects) or an error (which would
    // terminate the workflow).
    [[nodiscard]] bool is_suspended() const noexcept {
        return suspension.has_value();
    }

    // The single predicate every evaluator/executor short-circuit site checks:
    // unwind the recursion on either an error or a suspension. RFC 0022 slice 3
    // routes all propagation through this (a CI guard bans bare has_errors() in
    // the evaluator/executor) so no site can silently swallow a suspension.
    [[nodiscard]] bool should_unwind() const noexcept {
        return has_errors() || is_suspended();
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

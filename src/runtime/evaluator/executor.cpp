#include "runtime/evaluator/executor.hpp"

#include "runtime/evaluator/evaluator.hpp"

#include <string>
#include <variant>

namespace ahfl::evaluator {

// ============================================================================
// ExecContext implementation
// ============================================================================

void ExecContext::bind_local(const std::string &name, Value value) {
    eval_ctx.bind_local(name, std::move(value));
}

bool ExecContext::assign_ctx(const std::string &name, Value value) {
    eval_ctx.set_ctx(name, std::move(value));
    return true;
}

EvalResult ExecContext::eval_expression(const ir::Expr &expr) const {
    if (expr_eval) {
        return expr_eval(expr, eval_ctx);
    }
    return eval_expr(expr, eval_ctx);
}

// ============================================================================
// Helpers
// ============================================================================

namespace {

constexpr const char *kAssertFailedDefault = "assertion failed";
constexpr const char *kRequiresFailedDefault = "requires violation";
constexpr const char *kUnwrapNoneDefault = "unwrap failed: value is None";
constexpr const char *kUnreachableDefault = "unreachable executed";

ExecResult make_continue() {
    return ExecResult{ExecContinue{}, {}};
}

ExecResult make_exec_error(std::string message) {
    ExecResult result;
    result.outcome = ExecContinue{};
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

/// Evaluate the optional user message on an assert/requires/unreachable
/// statement.  Returns the evaluated string if the message expression is
/// present and evaluates cleanly to a StringValue; otherwise falls back to
/// `fallback` so the primary failure (assert false / unreachable) is never
/// masked by a secondary error in the diagnostic expression.
[[nodiscard]] std::string eval_failure_message(ExecContext &ctx,
                                               const ir::ExprRef &message_expr,
                                               std::string_view fallback) {
    if (!message_expr) {
        return std::string{fallback};
    }
    auto result = ctx.eval_expression(*message_expr);
    if (result.has_errors()) {
        return std::string{fallback};
    }
    if (auto *sv = std::get_if<StringValue>(&result.value.node); sv != nullptr) {
        return sv->value;
    }
    return std::string{fallback};
}

} // anonymous namespace

namespace {

/// Translate an EvalResult's error diagnostics into an ExecResult.
/// * If the eval produced a specific "unwrap failed: value is None" diagnostic
///   (raised by expression-level `unwrap(e)` when the operand is None), surface
///   it as an ExecAssertFailed so runtime callers / tests receive the same
///   failure shape as statement-level unwrap.
/// * Otherwise preserve the historical behaviour (ExecContinue + diagnostics).
[[nodiscard]] ExecResult wrap_expression_errors(EvalResult &&eval_result) {
    ExecResult r;
    // Scan the diagnostic bag for the unwrap-None sentinel message.
    bool found_unwrap_none = false;
    std::string unwrap_message = kUnwrapNoneDefault;
    // Walk the diagnostic bag via the public entries() view.
    for (const Diagnostic &diag : eval_result.diagnostics.entries()) {
        if (diag.message.find(kUnwrapNoneDefault) != std::string_view::npos) {
            found_unwrap_none = true;
            // Prefer the user-supplied message (if any) over the default.  The
            // evaluator concatenates the message to the default prefix; keep
            // the full text so callers never lose information.
            unwrap_message = std::string{diag.message};
        }
    }
    if (found_unwrap_none) {
        r.outcome = ExecAssertFailed{AssertionKind::UNWRAP_NONE,
                                     std::move(unwrap_message)};
        return r;
    }
    r.outcome = ExecContinue{};
    r.diagnostics = std::move(eval_result.diagnostics);
    return r;
}

} // anonymous namespace

// ============================================================================
// exec_statement - visit the StatementNode variant and dispatch execution
// ============================================================================

ExecResult exec_statement(const ir::Statement &stmt, ExecContext &ctx) {
    return std::visit(
        [&ctx](const auto &node) -> ExecResult {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ir::LetStatement>) {
                // Evaluate the initializer and bind it to local scope
                if (!node.initializer) {
                    return make_exec_error("LetStatement has null initializer");
                }
                auto eval_result = ctx.eval_expression(*node.initializer);
                if (eval_result.has_errors()) {
                    return wrap_expression_errors(std::move(eval_result));
                }
                ctx.bind_local(node.name, std::move(eval_result.value));
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::AssignStatement>) {
                // Only allow assignment to ctx.field paths
                const auto &path = node.target;
                if (path.root_kind != ir::PathRootKind::Context) {
                    return make_exec_error(
                        "assignment target must be a ctx field (e.g. ctx.field)");
                }
                if (path.members.empty()) {
                    return make_exec_error(
                        "assignment target must specify a field (e.g. ctx.field)");
                }
                if (!node.value) {
                    return make_exec_error("AssignStatement has null value expression");
                }
                auto eval_result = ctx.eval_expression(*node.value);
                if (eval_result.has_errors()) {
                    return wrap_expression_errors(std::move(eval_result));
                }
                ctx.assign_ctx(path.members[0], std::move(eval_result.value));
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::IfStatement>) {
                // Evaluate the condition expression
                if (!node.condition) {
                    return make_exec_error("IfStatement has null condition");
                }
                auto cond_result = ctx.eval_expression(*node.condition);
                if (cond_result.has_errors()) {
                    return wrap_expression_errors(std::move(cond_result));
                }
                auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
                if (!bv) {
                    return make_exec_error("if condition must evaluate to Bool");
                }
                if (bv->value) {
                    // then branch
                    if (node.then_block) {
                        return exec_block(*node.then_block, ctx);
                    }
                    return make_continue();
                }
                // else branch
                if (node.else_block) {
                    return exec_block(*node.else_block, ctx);
                }
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::GotoStatement>) {
                return ExecResult{ExecGoto{node.target_state}, {}};

            } else if constexpr (std::is_same_v<T, ir::ReturnStatement>) {
                if (!node.value) {
                    return ExecResult{ExecReturn{make_none()}, {}};
                }
                auto eval_result = ctx.eval_expression(*node.value);
                if (eval_result.has_errors()) {
                    return wrap_expression_errors(std::move(eval_result));
                }
                return ExecResult{ExecReturn{std::move(eval_result.value)}, {}};

            } else if constexpr (std::is_same_v<T, ir::AssertStatement>) {
                if (!node.condition) {
                    return make_exec_error("AssertStatement has null condition");
                }
                auto cond_result = ctx.eval_expression(*node.condition);
                if (cond_result.has_errors()) {
                    return wrap_expression_errors(std::move(cond_result));
                }
                auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
                if (!bv) {
                    return make_exec_error("assert condition must evaluate to Bool");
                }
                if (!bv->value) {
                    return ExecResult{
                        ExecAssertFailed{AssertionKind::ASSERT_CLAUSE,
                                         eval_failure_message(
                                             ctx, node.message, kAssertFailedDefault)},
                        {}};
                }
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::UnwrapStatement>) {
                // P4-01: "assert is Some" — fail if the operand is the None
                // variant of a nominal Option<T>, or a BoolValue{false} when
                // used in an ad-hoc truthiness context.  Value-extraction
                // (producing the T payload) is a follow-up.
                if (!node.operand) {
                    return make_exec_error("UnwrapStatement has null operand");
                }
                auto op_result = ctx.eval_expression(*node.operand);
                if (op_result.has_errors()) {
                    return wrap_expression_errors(std::move(op_result));
                }
                const bool is_some = [](const Value &v) {
                    if (std::holds_alternative<NoneValue>(v.node)) return false;
                    if (const auto *opt = std::get_if<OptionalValue>(&v.node)) {
                        return opt->inner != nullptr;
                    }
                    if (const auto *ev = std::get_if<EnumValue>(&v.node)) {
                        return ev->variant != "None";
                    }
                    // Any non-None, non-empty payload counts as truthy.
                    return true;
                }(op_result.value);
                if (!is_some) {
                    return ExecResult{
                        ExecAssertFailed{AssertionKind::UNWRAP_NONE, kUnwrapNoneDefault}, {}};
                }
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::RequiresStatement>) {
                if (!node.condition) {
                    return make_exec_error("RequiresStatement has null condition");
                }
                auto cond_result = ctx.eval_expression(*node.condition);
                if (cond_result.has_errors()) {
                    return wrap_expression_errors(std::move(cond_result));
                }
                auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
                if (!bv) {
                    return make_exec_error("requires condition must evaluate to Bool");
                }
                if (!bv->value) {
                    return ExecResult{
                        ExecAssertFailed{AssertionKind::REQUIRES_VIOLATION,
                                         eval_failure_message(
                                             ctx, node.message, kRequiresFailedDefault)},
                        {}};
                }
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::UnreachableStatement>) {
                // Unconditional runtime failure.  `unreachable;` is the user
                // asserting that this code path cannot be taken — executing it
                // means the caller's reasoning was wrong.
                return ExecResult{
                    ExecAssertFailed{AssertionKind::UNREACHABLE_EXECUTED,
                                     eval_failure_message(ctx, node.message, kUnreachableDefault)},
                    {}};

            } else if constexpr (std::is_same_v<T, ir::ExprStatement>) {
                // Evaluate the expression and discard the result
                if (!node.expr) {
                    return make_exec_error("ExprStatement has null expression");
                }
                auto eval_result = ctx.eval_expression(*node.expr);
                if (eval_result.has_errors()) {
                    return wrap_expression_errors(std::move(eval_result));
                }
                return make_continue();
            }
        },
        stmt.node);
}

// ============================================================================
// exec_block - execute statements in the Block sequentially, stopping on any non-Continue result
// ============================================================================

ExecResult exec_block(const ir::Block &block, ExecContext &ctx) {
    for (const auto &stmt_ptr : block.statements) {
        if (!stmt_ptr)
            continue;
        auto result = exec_statement(*stmt_ptr, ctx);
        // If there are errors or control flow is non-Continue, return immediately
        if (result.has_errors() || !std::holds_alternative<ExecContinue>(result.outcome)) {
            return result;
        }
    }
    return make_continue();
}

} // namespace ahfl::evaluator

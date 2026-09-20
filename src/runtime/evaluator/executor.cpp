#include "runtime/evaluator/executor.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/pattern_match.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

ExecResult make_continue(DiagnosticBag diagnostics = {}) {
    return ExecResult{ExecContinue{}, std::move(diagnostics)};
}

ExecResult make_exec_error(std::string message) {
    ExecResult result;
    result.outcome = ExecContinue{};
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

[[nodiscard]] ExecResult prepend_diagnostics(DiagnosticBag diagnostics, ExecResult result) {
    diagnostics.append(result.diagnostics);
    result.diagnostics = std::move(diagnostics);
    return result;
}

/// Evaluate the optional user message on an assert/requires/unreachable
/// statement.  Returns the evaluated string if the message expression is
/// present and evaluates cleanly to a StringValue; otherwise falls back to
/// `fallback` so the primary failure (assert false / unreachable) is never
/// masked by a secondary error in the diagnostic expression.
[[nodiscard]] std::string
eval_failure_message(ExecContext &ctx, const ir::ExprRef &message_expr, std::string_view fallback) {
    if (!message_expr) {
        return std::string{fallback};
    }
    auto result = ctx.eval_expression(*message_expr);
    if (result.should_unwind()) {
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
    // RFC 0022 slice 3: a suspended expression eval carries no error diagnostic
    // and no unwrap-None sentinel — propagate the suspension unchanged so it
    // unwinds through the agent state machine to the node loop.
    if (eval_result.is_suspended()) {
        r.outcome = ExecContinue{};
        r.diagnostics = std::move(eval_result.diagnostics);
        r.suspension = eval_result.suspension;
        return r;
    }
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
        r.outcome = ExecAssertFailed{AssertionKind::UNWRAP_NONE, std::move(unwrap_message)};
        return r;
    }
    r.outcome = ExecContinue{};
    r.diagnostics = std::move(eval_result.diagnostics);
    return r;
}

} // anonymous namespace

// RFC 0027 P6/P7/P8 (KR6.13-F): one NAMED per-node executor per StatementNode
// alternative, generated from the X-list stmt_nodes.def. Before this slice the
// whole variant was handled by a single generic-lambda if-constexpr chain, so a
// new statement node was silently accepted (its body simply fell off the end)
// instead of being routed. The explicit enumeration turns that into a COMPILE
// ERROR naming the type (CLAUDE.md Principle 5).

[[nodiscard]] ExecResult exec_statement_node(const ir::LetStatement &node, ExecContext &ctx) {
    // Evaluate the initializer and bind it to local scope
    if (!node.initializer) {
        return make_exec_error("LetStatement has null initializer");
    }
    auto eval_result = ctx.eval_expression(*node.initializer);
    if (eval_result.should_unwind()) {
        return wrap_expression_errors(std::move(eval_result));
    }
    ctx.bind_local(node.name, std::move(eval_result.value));
    return make_continue(std::move(eval_result.diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::AssignStatement &node, ExecContext &ctx) {
    // Only allow assignment to ctx.field paths
    const auto &path = node.target;
    if (path.root_kind != ir::PathRootKind::Context) {
        return make_exec_error("assignment target must be a ctx field (e.g. ctx.field)");
    }
    if (path.members.empty()) {
        return make_exec_error("assignment target must specify a field (e.g. ctx.field)");
    }
    if (!node.value) {
        return make_exec_error("AssignStatement has null value expression");
    }
    auto eval_result = ctx.eval_expression(*node.value);
    if (eval_result.should_unwind()) {
        return wrap_expression_errors(std::move(eval_result));
    }
    ctx.assign_ctx(path.members[0], std::move(eval_result.value));
    return make_continue(std::move(eval_result.diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::IfStatement &node, ExecContext &ctx) {
    // Evaluate the condition expression
    if (!node.condition) {
        return make_exec_error("IfStatement has null condition");
    }
    auto cond_result = ctx.eval_expression(*node.condition);
    if (cond_result.should_unwind()) {
        return wrap_expression_errors(std::move(cond_result));
    }
    auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
    if (!bv) {
        return prepend_diagnostics(std::move(cond_result.diagnostics),
                                   make_exec_error("if condition must evaluate to Bool"));
    }
    auto diagnostics = std::move(cond_result.diagnostics);
    if (bv->value) {
        // then branch
        if (node.then_block) {
            return prepend_diagnostics(std::move(diagnostics), exec_block(*node.then_block, ctx));
        }
        return make_continue(std::move(diagnostics));
    }
    // else branch
    if (node.else_block) {
        return prepend_diagnostics(std::move(diagnostics), exec_block(*node.else_block, ctx));
    }
    return make_continue(std::move(diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::IfLetStatement &node, ExecContext &ctx) {
    if (!node.scrutinee) {
        return make_exec_error("IfLetStatement has null scrutinee");
    }
    auto scrutinee_result = ctx.eval_expression(*node.scrutinee);
    if (scrutinee_result.should_unwind()) {
        return wrap_expression_errors(std::move(scrutinee_result));
    }
    auto diagnostics = std::move(scrutinee_result.diagnostics);

    PatternBindings bindings;
    if (match_pattern(node.pattern, scrutinee_result.value, bindings)) {
        std::unordered_map<std::string, std::optional<Value>> saved_bindings;
        saved_bindings.reserve(bindings.size());
        for (const auto &[name, _] : bindings) {
            saved_bindings.emplace(name, ctx.eval_ctx.get_local(name));
        }
        for (auto &[name, value] : bindings) {
            ctx.bind_local(name, std::move(value));
        }
        ExecResult result = node.then_block ? exec_block(*node.then_block, ctx) : make_continue();
        for (auto &[name, old_value] : saved_bindings) {
            if (old_value.has_value()) {
                ctx.bind_local(name, std::move(*old_value));
            } else {
                ctx.eval_ctx.erase_local(name);
            }
        }
        return prepend_diagnostics(std::move(diagnostics), std::move(result));
    }

    if (node.else_block) {
        return prepend_diagnostics(std::move(diagnostics), exec_block(*node.else_block, ctx));
    }
    return make_continue(std::move(diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::GotoStatement &node, ExecContext & /*ctx*/) {
    return ExecResult{ExecGoto{node.target_state}, {}};
}

[[nodiscard]] ExecResult exec_statement_node(const ir::ReturnStatement &node, ExecContext &ctx) {
    if (!node.value) {
        return ExecResult{ExecReturn{make_none()}, {}};
    }
    auto eval_result = ctx.eval_expression(*node.value);
    if (eval_result.should_unwind()) {
        return wrap_expression_errors(std::move(eval_result));
    }
    return ExecResult{
        ExecReturn{std::move(eval_result.value)},
        std::move(eval_result.diagnostics),
    };
}

[[nodiscard]] ExecResult exec_statement_node(const ir::AssertStatement &node, ExecContext &ctx) {
    if (!node.condition) {
        return make_exec_error("AssertStatement has null condition");
    }
    auto cond_result = ctx.eval_expression(*node.condition);
    if (cond_result.should_unwind()) {
        return wrap_expression_errors(std::move(cond_result));
    }
    auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
    if (!bv) {
        return prepend_diagnostics(std::move(cond_result.diagnostics),
                                   make_exec_error("assert condition must evaluate to Bool"));
    }
    if (!bv->value) {
        return ExecResult{
            ExecAssertFailed{AssertionKind::ASSERT_CLAUSE,
                             eval_failure_message(ctx, node.message, kAssertFailedDefault)},
            std::move(cond_result.diagnostics)};
    }
    return make_continue(std::move(cond_result.diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::UnwrapStatement &node, ExecContext &ctx) {
    // P4-01: "assert is Some" — fail if the operand is the None
    // variant of a nominal Option<T>, or a BoolValue{false} when
    // used in an ad-hoc truthiness context.  Value-extraction
    // (producing the T payload) is a follow-up.
    if (!node.operand) {
        return make_exec_error("UnwrapStatement has null operand");
    }
    auto op_result = ctx.eval_expression(*node.operand);
    if (op_result.should_unwind()) {
        return wrap_expression_errors(std::move(op_result));
    }
    const bool is_some = [](const Value &v) {
        if (is_optional(v)) {
            return ahfl::evaluator::is_some(v);
        }
        return true;
    }(op_result.value);
    if (!is_some) {
        return ExecResult{ExecAssertFailed{AssertionKind::UNWRAP_NONE, kUnwrapNoneDefault},
                          std::move(op_result.diagnostics)};
    }
    return make_continue(std::move(op_result.diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::RequiresStatement &node, ExecContext &ctx) {
    if (!node.condition) {
        return make_exec_error("RequiresStatement has null condition");
    }
    auto cond_result = ctx.eval_expression(*node.condition);
    if (cond_result.should_unwind()) {
        return wrap_expression_errors(std::move(cond_result));
    }
    auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
    if (!bv) {
        return prepend_diagnostics(std::move(cond_result.diagnostics),
                                   make_exec_error("requires condition must evaluate to Bool"));
    }
    if (!bv->value) {
        return ExecResult{
            ExecAssertFailed{
                AssertionKind::REQUIRES_VIOLATION,
                eval_failure_message(ctx, node.message, kRequiresFailedDefault)},
            std::move(cond_result.diagnostics)};
    }
    return make_continue(std::move(cond_result.diagnostics));
}

[[nodiscard]] ExecResult exec_statement_node(const ir::UnreachableStatement &node,
                                             ExecContext &ctx) {
    // Unconditional runtime failure.  `unreachable;` is the user
    // asserting that this code path cannot be taken — executing it
    // means the caller's reasoning was wrong.
    return ExecResult{
        ExecAssertFailed{AssertionKind::UNREACHABLE_EXECUTED,
                         eval_failure_message(ctx, node.message, kUnreachableDefault)},
        {}};
}

[[nodiscard]] ExecResult exec_statement_node(const ir::ExprStatement &node, ExecContext &ctx) {
    // Evaluate the expression and discard the result
    if (!node.expr) {
        return make_exec_error("ExprStatement has null expression");
    }
    auto eval_result = ctx.eval_expression(*node.expr);
    if (eval_result.should_unwind()) {
        return wrap_expression_errors(std::move(eval_result));
    }
    return make_continue(std::move(eval_result.diagnostics));
}

// ============================================================================
// exec_statement - visit the StatementNode variant and dispatch execution
// ============================================================================

ExecResult exec_statement(const ir::Statement &stmt, ExecContext &ctx) {
    // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per StatementNode alternative,
    // generated from the X-list stmt_nodes.def. Every node routes to its named
    // executor above; there is no generic catch-all, so a new statement node is a
    // COMPILE ERROR here until it is routed (CLAUDE.md Principle 5).
#define HANDLE_STMT_NODE(Name, Wire)                                                                  \
    [&ctx](const ir::Name &node) -> ExecResult { return exec_statement_node(node, ctx); },
    return std::visit(
        Overloaded{
#include "ahfl/compiler/ir/stmt_nodes.def"
        },
        stmt.node);
#undef HANDLE_STMT_NODE
}

// ============================================================================
// exec_block - execute statements in the Block sequentially, stopping on any non-Continue result
// ============================================================================

ExecResult exec_block(const ir::Block &block, ExecContext &ctx) {
    DiagnosticBag diagnostics;
    for (const auto &stmt_ptr : block.statements) {
        if (!stmt_ptr)
            continue;
        auto result = exec_statement(*stmt_ptr, ctx);
        diagnostics.append(result.diagnostics);
        // Return immediately on errors, a suspension (RFC 0022 — running the next
        // statement would be a further effect), or non-Continue control flow.
        if (result.should_unwind() || !std::holds_alternative<ExecContinue>(result.outcome)) {
            result.diagnostics = std::move(diagnostics);
            return result;
        }
    }
    return make_continue(std::move(diagnostics));
}

} // namespace ahfl::evaluator

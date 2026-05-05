#include "ahfl/evaluator/executor.hpp"

#include "ahfl/evaluator/evaluator.hpp"

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

ExecResult make_continue() {
    return ExecResult{ExecContinue{}, {}};
}

ExecResult make_exec_error(std::string message) {
    ExecResult result;
    result.outcome = ExecContinue{};
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

} // anonymous namespace

// ============================================================================
// exec_statement - 访问 StatementNode variant 并分发执行
// ============================================================================

ExecResult exec_statement(const ir::Statement &stmt, ExecContext &ctx) {
    return std::visit(
        [&ctx](const auto &node) -> ExecResult {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ir::LetStatement>) {
                // 求值 initializer 并绑定到 local scope
                if (!node.initializer) {
                    return make_exec_error("LetStatement has null initializer");
                }
                auto eval_result = ctx.eval_expression(*node.initializer);
                if (eval_result.has_errors()) {
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(eval_result.diagnostics);
                    return r;
                }
                ctx.bind_local(node.name, std::move(eval_result.value));
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::AssignStatement>) {
                // 仅允许赋值到 ctx.field 路径
                const auto &path = node.target;
                if (path.root_kind != ir::PathRootKind::Identifier || path.root_name != "ctx") {
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
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(eval_result.diagnostics);
                    return r;
                }
                ctx.assign_ctx(path.members[0], std::move(eval_result.value));
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::IfStatement>) {
                // 求值条件表达式
                if (!node.condition) {
                    return make_exec_error("IfStatement has null condition");
                }
                auto cond_result = ctx.eval_expression(*node.condition);
                if (cond_result.has_errors()) {
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(cond_result.diagnostics);
                    return r;
                }
                auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
                if (!bv) {
                    return make_exec_error("if condition must evaluate to Bool");
                }
                if (bv->value) {
                    // then 分支
                    if (node.then_block) {
                        return exec_block(*node.then_block, ctx);
                    }
                    return make_continue();
                }
                // else 分支
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
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(eval_result.diagnostics);
                    return r;
                }
                return ExecResult{ExecReturn{std::move(eval_result.value)}, {}};

            } else if constexpr (std::is_same_v<T, ir::AssertStatement>) {
                if (!node.condition) {
                    return make_exec_error("AssertStatement has null condition");
                }
                auto cond_result = ctx.eval_expression(*node.condition);
                if (cond_result.has_errors()) {
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(cond_result.diagnostics);
                    return r;
                }
                auto *bv = std::get_if<BoolValue>(&cond_result.value.node);
                if (!bv) {
                    return make_exec_error("assert condition must evaluate to Bool");
                }
                if (!bv->value) {
                    return ExecResult{ExecAssertFailed{"assertion failed"}, {}};
                }
                return make_continue();

            } else if constexpr (std::is_same_v<T, ir::ExprStatement>) {
                // 求值表达式并丢弃结果
                if (!node.expr) {
                    return make_exec_error("ExprStatement has null expression");
                }
                auto eval_result = ctx.eval_expression(*node.expr);
                if (eval_result.has_errors()) {
                    ExecResult r;
                    r.outcome = ExecContinue{};
                    r.diagnostics = std::move(eval_result.diagnostics);
                    return r;
                }
                return make_continue();
            }
        },
        stmt.node);
}

// ============================================================================
// exec_block - 顺序执行 Block 中的语句，遇到非 Continue 结果时停止
// ============================================================================

ExecResult exec_block(const ir::Block &block, ExecContext &ctx) {
    for (const auto &stmt_ptr : block.statements) {
        if (!stmt_ptr) continue;
        auto result = exec_statement(*stmt_ptr, ctx);
        // 如果有错误或控制流非 Continue，立即返回
        if (result.has_errors() || !std::holds_alternative<ExecContinue>(result.outcome)) {
            return result;
        }
    }
    return make_continue();
}

} // namespace ahfl::evaluator

#include "ahfl/evaluator/executor.hpp"
#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/support/ownership.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::ir;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

// Helper: make IR ExprPtr
ExprPtr make_expr_ptr(ExprNode node) {
    return std::make_unique<Expr>(Expr{std::move(node)});
}

// Helper: make a Statement from StatementNode
Statement make_stmt(StatementNode node) {
    return Statement{std::move(node)};
}

StatementPtr make_stmt_ptr(StatementNode node) {
    return std::make_unique<Statement>(Statement{std::move(node)});
}

// ============================================================================
// LetStatement Tests
// ============================================================================

void test_let_statement_binds_variable() {
    ExecContext ctx;
    // let x = 42
    auto stmt = make_stmt(LetStatement{"x", "Int", make_expr_ptr(IntegerLiteralExpr{"42"})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "let.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "let.continues");
    // 验证 local scope 中有 x = 42
    auto val = ctx.eval_ctx.get_local("x");
    check(val.has_value(), "let.x_bound");
    auto *iv = std::get_if<IntValue>(&val->node);
    check(iv != nullptr && iv->value == 42, "let.x_is_42");
}

// ============================================================================
// AssignStatement Tests
// ============================================================================

void test_assign_ctx_field() {
    ExecContext ctx;
    ctx.eval_ctx.set_ctx("result", make_string("old"));
    // ctx.result = "new"
    Path path;
    path.root_kind = PathRootKind::Identifier;
    path.root_name = "ctx";
    path.members = {"result"};
    auto stmt = make_stmt(AssignStatement{path, make_expr_ptr(StringLiteralExpr{"new"})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "assign.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "assign.continues");
    auto val = ctx.eval_ctx.get_ctx("result");
    check(val.has_value(), "assign.result_exists");
    auto *sv = std::get_if<StringValue>(&val->node);
    check(sv != nullptr && sv->value == "new", "assign.result_is_new");
}

void test_assign_non_ctx_path_error() {
    ExecContext ctx;
    // input.field = 1 (not allowed)
    Path path;
    path.root_kind = PathRootKind::Identifier;
    path.root_name = "input";
    path.members = {"field"};
    auto stmt = make_stmt(AssignStatement{path, make_expr_ptr(IntegerLiteralExpr{"1"})});
    auto result = exec_statement(stmt, ctx);
    check(result.has_errors(), "assign_non_ctx.has_error");
}

// ============================================================================
// IfStatement Tests
// ============================================================================

void test_if_true_branch() {
    ExecContext ctx;
    // if true { goto Done }
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    auto stmt = make_stmt(IfStatement{
        make_expr_ptr(BoolLiteralExpr{true}),
        std::make_unique<Block>(std::move(then_block)),
        nullptr});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_true.no_error");
    auto *g = std::get_if<ExecGoto>(&result.outcome);
    check(g != nullptr && g->target_state == "Done", "if_true.goto_done");
}

void test_if_false_branch_no_else() {
    ExecContext ctx;
    // if false { goto Done }  => Continue
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    auto stmt = make_stmt(IfStatement{
        make_expr_ptr(BoolLiteralExpr{false}),
        std::make_unique<Block>(std::move(then_block)),
        nullptr});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_false_no_else.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "if_false_no_else.continues");
}

void test_if_false_with_else_block() {
    ExecContext ctx;
    // if false { goto A } else { goto B }
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"A"}));
    Block else_block;
    else_block.statements.push_back(make_stmt_ptr(GotoStatement{"B"}));
    auto stmt = make_stmt(IfStatement{
        make_expr_ptr(BoolLiteralExpr{false}),
        std::make_unique<Block>(std::move(then_block)),
        std::make_unique<Block>(std::move(else_block))});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_else.no_error");
    auto *g = std::get_if<ExecGoto>(&result.outcome);
    check(g != nullptr && g->target_state == "B", "if_else.goto_B");
}

void test_if_condition_not_bool_error() {
    ExecContext ctx;
    // if 42 { goto Done }
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    auto stmt = make_stmt(IfStatement{
        make_expr_ptr(IntegerLiteralExpr{"42"}),
        std::make_unique<Block>(std::move(then_block)),
        nullptr});
    auto result = exec_statement(stmt, ctx);
    check(result.has_errors(), "if_not_bool.has_error");
}

// ============================================================================
// GotoStatement Tests
// ============================================================================

void test_goto_statement() {
    ExecContext ctx;
    auto stmt = make_stmt(GotoStatement{"Approved"});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "goto.no_error");
    auto *g = std::get_if<ExecGoto>(&result.outcome);
    check(g != nullptr && g->target_state == "Approved", "goto.target_approved");
}

// ============================================================================
// ReturnStatement Tests
// ============================================================================

void test_return_statement_with_value() {
    ExecContext ctx;
    auto stmt = make_stmt(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"100"})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "return.no_error");
    auto *ret = std::get_if<ExecReturn>(&result.outcome);
    check(ret != nullptr, "return.is_return");
    if (ret) {
        auto *iv = std::get_if<IntValue>(&ret->value.node);
        check(iv != nullptr && iv->value == 100, "return.value_100");
    }
}

// ============================================================================
// AssertStatement Tests
// ============================================================================

void test_assert_pass() {
    ExecContext ctx;
    auto stmt = make_stmt(AssertStatement{make_expr_ptr(BoolLiteralExpr{true})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "assert_pass.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "assert_pass.continues");
}

void test_assert_fail() {
    ExecContext ctx;
    auto stmt = make_stmt(AssertStatement{make_expr_ptr(BoolLiteralExpr{false})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "assert_fail.no_diag_error");
    auto *af = std::get_if<ExecAssertFailed>(&result.outcome);
    check(af != nullptr, "assert_fail.is_assert_failed");
}

// ============================================================================
// ExprStatement Tests
// ============================================================================

void test_expr_statement_discards_value() {
    ExecContext ctx;
    auto stmt = make_stmt(ExprStatement{make_expr_ptr(IntegerLiteralExpr{"999"})});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "expr_stmt.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "expr_stmt.continues");
}

// ============================================================================
// Block Execution Tests
// ============================================================================

void test_block_stops_at_goto() {
    ExecContext ctx;
    Block block;
    block.statements.push_back(
        make_stmt_ptr(LetStatement{"x", "Int", make_expr_ptr(IntegerLiteralExpr{"1"})}));
    block.statements.push_back(make_stmt_ptr(GotoStatement{"Next"}));
    block.statements.push_back(
        make_stmt_ptr(LetStatement{"y", "Int", make_expr_ptr(IntegerLiteralExpr{"2"})}));
    auto result = exec_block(block, ctx);
    check(!result.has_errors(), "block_goto.no_error");
    auto *g = std::get_if<ExecGoto>(&result.outcome);
    check(g != nullptr && g->target_state == "Next", "block_goto.stopped_at_goto");
    // y should NOT be bound
    auto val = ctx.eval_ctx.get_local("y");
    check(!val.has_value(), "block_goto.y_not_bound");
}

void test_block_stops_at_return() {
    ExecContext ctx;
    Block block;
    block.statements.push_back(
        make_stmt_ptr(LetStatement{"x", "Int", make_expr_ptr(IntegerLiteralExpr{"5"})}));
    block.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"99"})}));
    block.statements.push_back(
        make_stmt_ptr(LetStatement{"y", "Int", make_expr_ptr(IntegerLiteralExpr{"2"})}));
    auto result = exec_block(block, ctx);
    check(!result.has_errors(), "block_return.no_error");
    auto *ret = std::get_if<ExecReturn>(&result.outcome);
    check(ret != nullptr, "block_return.is_return");
    auto val = ctx.eval_ctx.get_local("y");
    check(!val.has_value(), "block_return.y_not_bound");
}

// ============================================================================
// Nested if with goto
// ============================================================================

void test_nested_if_with_goto() {
    ExecContext ctx;
    ctx.eval_ctx.bind_local("flag", make_bool(true));
    // if flag { goto Target }
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"Target"}));

    // 构造路径表达式 "flag"
    PathExpr path_expr;
    path_expr.path.root_kind = PathRootKind::Identifier;
    path_expr.path.root_name = "flag";

    auto stmt = make_stmt(IfStatement{
        make_expr_ptr(std::move(path_expr)),
        std::make_unique<Block>(std::move(then_block)),
        nullptr});
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "nested_if_goto.no_error");
    auto *g = std::get_if<ExecGoto>(&result.outcome);
    check(g != nullptr && g->target_state == "Target", "nested_if_goto.goto_target");
}

} // anonymous namespace

int main() {
    test_let_statement_binds_variable();
    test_assign_ctx_field();
    test_assign_non_ctx_path_error();
    test_if_true_branch();
    test_if_false_branch_no_else();
    test_if_false_with_else_block();
    test_if_condition_not_bool_error();
    test_goto_statement();
    test_return_statement_with_value();
    test_assert_pass();
    test_assert_fail();
    test_expr_statement_discards_value();
    test_block_stops_at_goto();
    test_block_stops_at_return();
    test_nested_if_with_goto();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}

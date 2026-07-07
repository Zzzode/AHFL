#include "runtime/evaluator/executor.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/value.hpp"

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

// Helper: make IR ExprRef
ExprArena &test_expr_arena() {
    static ExprArena arena;
    return arena;
}

ExprRef make_expr_ptr(ExprNode node) {
    return test_expr_arena().make(std::move(node));
}

// Helper: make a Statement from StatementNode
Statement make_stmt(StatementNode node) {
    return Statement{std::move(node), {}};
}

StatementPtr make_stmt_ptr(StatementNode node) {
    return std::make_unique<Statement>(Statement{std::move(node), {}});
}

TypeRef make_int_type_ref() {
    return TypeRef{
        .kind = TypeRefKind::Int,
        .display_name = "Int",
        .canonical_name = "Int",
    };
}

ExprRef make_local_path_expr(std::string name) {
    PathExpr path_expr;
    path_expr.path.root_kind = PathRootKind::Local;
    path_expr.path.root_name = std::move(name);
    return make_expr_ptr(std::move(path_expr));
}

MatchPattern make_some_binding_pattern(std::string binding_name) {
    VariantPattern variant;
    variant.path = "Some";
    variant.kind = VariantPatternKind::Tuple;
    const auto pattern_text = binding_name;
    variant.subpatterns.push_back(std::make_unique<MatchPattern>(MatchPattern{
        .node = BindingPattern{.name = std::move(binding_name), .is_mut = false, .nested = nullptr},
        .source_range = std::nullopt,
        .text = pattern_text,
    }));
    return MatchPattern{
        .node = std::move(variant),
        .source_range = std::nullopt,
        .text = "Some(" + pattern_text + ")",
    };
}

// ============================================================================
// LetStatement Tests
// ============================================================================

void test_let_statement_binds_variable() {
    ExecContext ctx;
    // let x = 42
    auto stmt = make_stmt(LetStatement{
        .name = "x",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"42"}),
    });
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "let.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "let.continues");
    // Verify the local scope contains x = 42
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
    path.root_kind = PathRootKind::Context;
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
    auto stmt = make_stmt(IfStatement{make_expr_ptr(BoolLiteralExpr{true}),
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
    auto stmt = make_stmt(IfStatement{make_expr_ptr(BoolLiteralExpr{false}),
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
    auto stmt = make_stmt(IfStatement{make_expr_ptr(BoolLiteralExpr{false}),
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
    auto stmt = make_stmt(IfStatement{make_expr_ptr(IntegerLiteralExpr{"42"}),
                                      std::make_unique<Block>(std::move(then_block)),
                                      nullptr});
    auto result = exec_statement(stmt, ctx);
    check(result.has_errors(), "if_not_bool.has_error");
}

void test_if_let_some_binds_payload_for_then_branch() {
    ExecContext ctx;
    std::vector<Value> payload;
    payload.push_back(make_int(7));
    ctx.eval_ctx.bind_local("maybe", make_enum("std::option::Option", "Some", std::move(payload)));

    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(ReturnStatement{make_local_path_expr("x")}));
    Block else_block;
    else_block.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"0"})}));

    auto stmt = make_stmt(IfLetStatement{
        .pattern = make_some_binding_pattern("x"),
        .scrutinee = make_local_path_expr("maybe"),
        .then_block = std::make_unique<Block>(std::move(then_block)),
        .else_block = std::make_unique<Block>(std::move(else_block)),
    });
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_let_some.no_error");
    auto *ret = std::get_if<ExecReturn>(&result.outcome);
    check(ret != nullptr, "if_let_some.returns");
    auto *iv = ret != nullptr ? std::get_if<IntValue>(&ret->value.node) : nullptr;
    check(iv != nullptr && iv->value == 7, "if_let_some.payload_returned");
    check(!ctx.eval_ctx.get_local("x").has_value(), "if_let_some.payload_not_leaked");
}

void test_if_let_non_matching_variant_runs_else_branch() {
    ExecContext ctx;
    ctx.eval_ctx.bind_local("maybe", make_enum("std::option::Option", "None"));

    Block then_block;
    then_block.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"1"})}));
    Block else_block;
    else_block.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"0"})}));

    auto stmt = make_stmt(IfLetStatement{
        .pattern = make_some_binding_pattern("x"),
        .scrutinee = make_local_path_expr("maybe"),
        .then_block = std::make_unique<Block>(std::move(then_block)),
        .else_block = std::make_unique<Block>(std::move(else_block)),
    });
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_let_else.no_error");
    auto *ret = std::get_if<ExecReturn>(&result.outcome);
    check(ret != nullptr, "if_let_else.returns");
    auto *iv = ret != nullptr ? std::get_if<IntValue>(&ret->value.node) : nullptr;
    check(iv != nullptr && iv->value == 0, "if_let_else.else_returned");
    check(!ctx.eval_ctx.get_local("x").has_value(), "if_let_else.no_payload_binding");
}

void test_if_let_restores_only_payload_bindings() {
    ExecContext ctx;
    std::vector<Value> payload;
    payload.push_back(make_int(7));
    ctx.eval_ctx.bind_local("maybe", make_enum("std::option::Option", "Some", std::move(payload)));
    ctx.eval_ctx.bind_local("keep", make_int(1));

    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(LetStatement{
        .name = "keep",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"2"}),
    }));

    auto stmt = make_stmt(IfLetStatement{
        .pattern = make_some_binding_pattern("x"),
        .scrutinee = make_local_path_expr("maybe"),
        .then_block = std::make_unique<Block>(std::move(then_block)),
        .else_block = nullptr,
    });
    auto result = exec_statement(stmt, ctx);
    check(!result.has_errors(), "if_let_scope.no_error");
    check(std::holds_alternative<ExecContinue>(result.outcome), "if_let_scope.continues");
    auto keep = ctx.eval_ctx.get_local("keep");
    auto *iv = keep.has_value() ? std::get_if<IntValue>(&keep->node) : nullptr;
    check(iv != nullptr && iv->value == 2, "if_let_scope.keep_updated");
    check(!ctx.eval_ctx.get_local("x").has_value(), "if_let_scope.payload_not_leaked");
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
    check(af != nullptr && af->kind == AssertionKind::ASSERT_CLAUSE,
          "assert_fail.kind_is_assert_clause");
    check(af != nullptr && !af->message.empty(), "assert_fail.message_non_empty");
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
    block.statements.push_back(make_stmt_ptr(LetStatement{
        .name = "x",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"1"}),
    }));
    block.statements.push_back(make_stmt_ptr(GotoStatement{"Next"}));
    block.statements.push_back(make_stmt_ptr(LetStatement{
        .name = "y",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"2"}),
    }));
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
    block.statements.push_back(make_stmt_ptr(LetStatement{
        .name = "x",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"5"}),
    }));
    block.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{"99"})}));
    block.statements.push_back(make_stmt_ptr(LetStatement{
        .name = "y",
        .type_ref = make_int_type_ref(),
        .initializer = make_expr_ptr(IntegerLiteralExpr{"2"}),
    }));
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

    // Build the path expression "flag"
    PathExpr path_expr;
    path_expr.path.root_kind = PathRootKind::Local;
    path_expr.path.root_name = "flag";

    auto stmt = make_stmt(IfStatement{make_expr_ptr(std::move(path_expr)),
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
    test_if_let_some_binds_payload_for_then_branch();
    test_if_let_non_matching_variant_runs_else_branch();
    test_if_let_restores_only_payload_bindings();
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

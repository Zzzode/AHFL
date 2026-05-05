#include "ahfl/evaluator/evaluator.hpp"
#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/support/ownership.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

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

// Helper: make IR Expr from ExprNode
Expr make_expr(ExprNode node) {
    return Expr{std::move(node)};
}

ExprPtr make_expr_ptr(ExprNode node) {
    return std::make_unique<Expr>(Expr{std::move(node)});
}

// ============================================================================
// Literal Tests
// ============================================================================

void test_none_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(NoneLiteralExpr{}), ctx);
    check(!result.has_errors(), "none_literal.no_error");
    check(is_none(result.value), "none_literal.is_none");
}

void test_bool_literal() {
    EvalContext ctx;
    auto result_true = eval_expr(make_expr(BoolLiteralExpr{true}), ctx);
    check(!result_true.has_errors(), "bool_literal_true.no_error");
    auto *bv = std::get_if<BoolValue>(&result_true.value.node);
    check(bv != nullptr && bv->value == true, "bool_literal_true.value");

    auto result_false = eval_expr(make_expr(BoolLiteralExpr{false}), ctx);
    check(!result_false.has_errors(), "bool_literal_false.no_error");
    auto *bv2 = std::get_if<BoolValue>(&result_false.value.node);
    check(bv2 != nullptr && bv2->value == false, "bool_literal_false.value");
}

void test_integer_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(IntegerLiteralExpr{"42"}), ctx);
    check(!result.has_errors(), "int_literal.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 42, "int_literal.value");

    auto neg = eval_expr(make_expr(IntegerLiteralExpr{"-10"}), ctx);
    check(!neg.has_errors(), "int_literal_neg.no_error");
    auto *nv = std::get_if<IntValue>(&neg.value.node);
    check(nv != nullptr && nv->value == -10, "int_literal_neg.value");
}

void test_float_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(FloatLiteralExpr{"3.14"}), ctx);
    check(!result.has_errors(), "float_literal.no_error");
    auto *fv = std::get_if<FloatValue>(&result.value.node);
    check(fv != nullptr && fv->value > 3.13 && fv->value < 3.15, "float_literal.value");
}

void test_decimal_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(DecimalLiteralExpr{"99.999"}), ctx);
    check(!result.has_errors(), "decimal_literal.no_error");
    auto *dv = std::get_if<DecimalValue>(&result.value.node);
    check(dv != nullptr && dv->spelling == "99.999", "decimal_literal.value");
}

void test_string_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(StringLiteralExpr{"hello world"}), ctx);
    check(!result.has_errors(), "string_literal.no_error");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "hello world", "string_literal.value");
}

void test_duration_literal() {
    EvalContext ctx;
    auto result = eval_expr(make_expr(DurationLiteralExpr{"5s"}), ctx);
    check(!result.has_errors(), "duration_literal.no_error");
    auto *dv = std::get_if<DurationValue>(&result.value.node);
    check(dv != nullptr && dv->spelling == "5s", "duration_literal.value");
}

// ============================================================================
// Unary Operator Tests
// ============================================================================

void test_unary_not() {
    EvalContext ctx;
    auto expr = make_expr(UnaryExpr{ExprUnaryOp::Not, make_expr_ptr(BoolLiteralExpr{true})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "unary_not.no_error");
    auto *bv = std::get_if<BoolValue>(&result.value.node);
    check(bv != nullptr && bv->value == false, "unary_not.value");
}

void test_unary_negate() {
    EvalContext ctx;
    auto expr = make_expr(UnaryExpr{ExprUnaryOp::Negate, make_expr_ptr(IntegerLiteralExpr{"5"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "unary_negate.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == -5, "unary_negate.value");
}

void test_unary_positive() {
    EvalContext ctx;
    auto expr =
        make_expr(UnaryExpr{ExprUnaryOp::Positive, make_expr_ptr(IntegerLiteralExpr{"7"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "unary_positive.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 7, "unary_positive.value");
}

// ============================================================================
// Binary Operator Tests
// ============================================================================

void test_binary_add() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Add,
                                     make_expr_ptr(IntegerLiteralExpr{"3"}),
                                     make_expr_ptr(IntegerLiteralExpr{"4"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_add_int.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 7, "binary_add_int.value");
}

void test_binary_add_float() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Add,
                                     make_expr_ptr(IntegerLiteralExpr{"3"}),
                                     make_expr_ptr(FloatLiteralExpr{"1.5"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_add_float.no_error");
    auto *fv = std::get_if<FloatValue>(&result.value.node);
    check(fv != nullptr && fv->value > 4.4 && fv->value < 4.6, "binary_add_float.value");
}

void test_binary_subtract() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Subtract,
                                     make_expr_ptr(IntegerLiteralExpr{"10"}),
                                     make_expr_ptr(IntegerLiteralExpr{"3"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_sub.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 7, "binary_sub.value");
}

void test_binary_multiply() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Multiply,
                                     make_expr_ptr(IntegerLiteralExpr{"6"}),
                                     make_expr_ptr(IntegerLiteralExpr{"7"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_mul.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 42, "binary_mul.value");
}

void test_binary_divide() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Divide,
                                     make_expr_ptr(IntegerLiteralExpr{"10"}),
                                     make_expr_ptr(IntegerLiteralExpr{"3"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_div.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 3, "binary_div.value");
}

void test_binary_modulo() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Modulo,
                                     make_expr_ptr(IntegerLiteralExpr{"10"}),
                                     make_expr_ptr(IntegerLiteralExpr{"3"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "binary_mod.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 1, "binary_mod.value");
}

void test_binary_comparison() {
    EvalContext ctx;
    auto lt = make_expr(BinaryExpr{ExprBinaryOp::Less,
                                   make_expr_ptr(IntegerLiteralExpr{"1"}),
                                   make_expr_ptr(IntegerLiteralExpr{"2"})});
    auto lt_result = eval_expr(lt, ctx);
    check(!lt_result.has_errors(), "binary_lt.no_error");
    auto *bv = std::get_if<BoolValue>(&lt_result.value.node);
    check(bv != nullptr && bv->value == true, "binary_lt.value");

    auto gt = make_expr(BinaryExpr{ExprBinaryOp::Greater,
                                   make_expr_ptr(IntegerLiteralExpr{"5"}),
                                   make_expr_ptr(IntegerLiteralExpr{"3"})});
    auto gt_result = eval_expr(gt, ctx);
    check(!gt_result.has_errors(), "binary_gt.no_error");
    auto *bv2 = std::get_if<BoolValue>(&gt_result.value.node);
    check(bv2 != nullptr && bv2->value == true, "binary_gt.value");
}

void test_binary_equality() {
    EvalContext ctx;
    auto eq = make_expr(BinaryExpr{ExprBinaryOp::Equal,
                                   make_expr_ptr(IntegerLiteralExpr{"5"}),
                                   make_expr_ptr(IntegerLiteralExpr{"5"})});
    auto eq_result = eval_expr(eq, ctx);
    check(!eq_result.has_errors(), "binary_eq.no_error");
    auto *bv = std::get_if<BoolValue>(&eq_result.value.node);
    check(bv != nullptr && bv->value == true, "binary_eq.value");

    auto neq = make_expr(BinaryExpr{ExprBinaryOp::NotEqual,
                                    make_expr_ptr(IntegerLiteralExpr{"5"}),
                                    make_expr_ptr(IntegerLiteralExpr{"3"})});
    auto neq_result = eval_expr(neq, ctx);
    check(!neq_result.has_errors(), "binary_neq.no_error");
    auto *bv2 = std::get_if<BoolValue>(&neq_result.value.node);
    check(bv2 != nullptr && bv2->value == true, "binary_neq.value");
}

void test_binary_logical() {
    EvalContext ctx;
    auto and_expr = make_expr(BinaryExpr{ExprBinaryOp::And,
                                         make_expr_ptr(BoolLiteralExpr{true}),
                                         make_expr_ptr(BoolLiteralExpr{false})});
    auto and_result = eval_expr(and_expr, ctx);
    check(!and_result.has_errors(), "binary_and.no_error");
    auto *bv = std::get_if<BoolValue>(&and_result.value.node);
    check(bv != nullptr && bv->value == false, "binary_and.value");

    auto or_expr = make_expr(BinaryExpr{ExprBinaryOp::Or,
                                        make_expr_ptr(BoolLiteralExpr{true}),
                                        make_expr_ptr(BoolLiteralExpr{false})});
    auto or_result = eval_expr(or_expr, ctx);
    check(!or_result.has_errors(), "binary_or.no_error");
    auto *bv2 = std::get_if<BoolValue>(&or_result.value.node);
    check(bv2 != nullptr && bv2->value == true, "binary_or.value");

    // implies: false => X is always true
    auto imp_expr = make_expr(BinaryExpr{ExprBinaryOp::Implies,
                                         make_expr_ptr(BoolLiteralExpr{false}),
                                         make_expr_ptr(BoolLiteralExpr{false})});
    auto imp_result = eval_expr(imp_expr, ctx);
    check(!imp_result.has_errors(), "binary_implies.no_error");
    auto *bv3 = std::get_if<BoolValue>(&imp_result.value.node);
    check(bv3 != nullptr && bv3->value == true, "binary_implies.value");
}

void test_binary_divide_by_zero() {
    EvalContext ctx;
    auto expr = make_expr(BinaryExpr{ExprBinaryOp::Divide,
                                     make_expr_ptr(IntegerLiteralExpr{"10"}),
                                     make_expr_ptr(IntegerLiteralExpr{"0"})});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "binary_div_zero.has_error");
}

// ============================================================================
// Path Access Tests
// ============================================================================

void test_path_input() {
    EvalContext ctx;
    ctx.set_input("order_id", make_string("abc-123"));

    Path path;
    path.root_name = "input";
    path.members = {"order_id"};
    auto expr = make_expr(PathExpr{path});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "path_input.no_error");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "abc-123", "path_input.value");
}

void test_path_ctx() {
    EvalContext ctx;
    ctx.set_ctx("result", make_int(42));

    Path path;
    path.root_name = "ctx";
    path.members = {"result"};
    auto expr = make_expr(PathExpr{path});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "path_ctx.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 42, "path_ctx.value");
}

void test_path_node_output() {
    EvalContext ctx;
    ctx.set_node_output("fetch_order", "status", make_string("completed"));

    Path path;
    path.root_name = "fetch_order";
    path.members = {"status"};
    auto expr = make_expr(PathExpr{path});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "path_node_output.no_error");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "completed", "path_node_output.value");
}

void test_path_unresolved() {
    EvalContext ctx;
    Path path;
    path.root_name = "input";
    path.members = {"nonexistent"};
    auto expr = make_expr(PathExpr{path});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "path_unresolved.has_error");
}

// ============================================================================
// Struct/List Literal Tests
// ============================================================================

void test_struct_literal() {
    EvalContext ctx;
    std::vector<StructFieldInit> fields;
    fields.push_back(StructFieldInit{"name", make_expr_ptr(StringLiteralExpr{"Alice"})});
    fields.push_back(StructFieldInit{"age", make_expr_ptr(IntegerLiteralExpr{"30"})});

    auto expr = make_expr(StructLiteralExpr{"Person", std::move(fields)});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "struct_literal.no_error");
    auto *sv = std::get_if<StructValue>(&result.value.node);
    check(sv != nullptr, "struct_literal.is_struct");
    check(sv != nullptr && sv->type_name == "Person", "struct_literal.type_name");
    if (sv) {
        auto it = sv->fields.find("name");
        check(it != sv->fields.end(), "struct_literal.has_name");
        if (it != sv->fields.end() && it->second) {
            auto *name_val = std::get_if<StringValue>(&it->second->node);
            check(name_val != nullptr && name_val->value == "Alice", "struct_literal.name_value");
        }
    }
}

void test_list_literal() {
    EvalContext ctx;
    std::vector<ExprPtr> items;
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"3"}));

    auto expr = make_expr(ListLiteralExpr{std::move(items)});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "list_literal.no_error");
    auto *lv = std::get_if<ListValue>(&result.value.node);
    check(lv != nullptr, "list_literal.is_list");
    check(lv != nullptr && lv->items.size() == 3, "list_literal.size");
}

// ============================================================================
// MemberAccess / IndexAccess Tests
// ============================================================================

void test_member_access() {
    EvalContext ctx;
    std::unordered_map<std::string, Value> fields;
    fields.emplace("name", make_string("Bob"));
    ctx.bind_local("person", make_struct("Person", std::move(fields)));

    // Access person.name
    Path path;
    path.root_name = "person";
    auto base_expr = make_expr_ptr(PathExpr{path});
    auto expr = make_expr(MemberAccessExpr{std::move(base_expr), "name"});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "member_access.no_error");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "Bob", "member_access.value");
}

void test_list_length() {
    EvalContext ctx;
    std::vector<Value> items;
    items.push_back(make_int(1));
    items.push_back(make_int(2));
    ctx.bind_local("arr", make_list(std::move(items)));

    Path path;
    path.root_name = "arr";
    auto base_expr = make_expr_ptr(PathExpr{path});
    auto expr = make_expr(MemberAccessExpr{std::move(base_expr), "length"});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "list_length.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 2, "list_length.value");
}

void test_index_access() {
    EvalContext ctx;
    std::vector<Value> items;
    items.push_back(make_string("a"));
    items.push_back(make_string("b"));
    items.push_back(make_string("c"));
    ctx.bind_local("arr", make_list(std::move(items)));

    Path path;
    path.root_name = "arr";
    auto base_expr = make_expr_ptr(PathExpr{path});
    auto index_expr = make_expr_ptr(IntegerLiteralExpr{"1"});
    auto expr = make_expr(IndexAccessExpr{std::move(base_expr), std::move(index_expr)});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "index_access.no_error");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "b", "index_access.value");
}

void test_index_out_of_bounds() {
    EvalContext ctx;
    std::vector<Value> items;
    items.push_back(make_int(1));
    ctx.bind_local("arr", make_list(std::move(items)));

    Path path;
    path.root_name = "arr";
    auto base_expr = make_expr_ptr(PathExpr{path});
    auto index_expr = make_expr_ptr(IntegerLiteralExpr{"5"});
    auto expr = make_expr(IndexAccessExpr{std::move(base_expr), std::move(index_expr)});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "index_oob.has_error");
}

// ============================================================================
// Optional / Qualified / Group Tests
// ============================================================================

void test_some_expr() {
    EvalContext ctx;
    auto expr = make_expr(SomeExpr{make_expr_ptr(IntegerLiteralExpr{"42"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "some_expr.no_error");
    auto *ov = std::get_if<OptionalValue>(&result.value.node);
    check(ov != nullptr && ov->inner != nullptr, "some_expr.has_inner");
    if (ov && ov->inner) {
        auto *iv = std::get_if<IntValue>(&ov->inner->node);
        check(iv != nullptr && iv->value == 42, "some_expr.inner_value");
    }
}

void test_qualified_value() {
    EvalContext ctx;
    auto expr = make_expr(QualifiedValueExpr{"AuditResult::Approve"});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "qualified_value.no_error");
    auto *ev = std::get_if<EnumValue>(&result.value.node);
    check(ev != nullptr && ev->enum_name == "AuditResult" && ev->variant == "Approve",
          "qualified_value.value");
}

void test_group_expr() {
    EvalContext ctx;
    auto expr = make_expr(GroupExpr{make_expr_ptr(IntegerLiteralExpr{"99"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "group_expr.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 99, "group_expr.value");
}

// ============================================================================
// Error Cases
// ============================================================================

void test_call_expr_error() {
    EvalContext ctx;
    auto expr = make_expr(CallExpr{"some_function", {}});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "call_expr.has_error");
}

// ============================================================================
// print_value test
// ============================================================================

void test_print_value() {
    std::ostringstream oss;
    auto val = make_int(42);
    print_value(val, oss);
    check(oss.str() == "42", "print_value.int");

    oss.str("");
    auto sv = make_string("hello");
    print_value(sv, oss);
    check(oss.str() == "\"hello\"", "print_value.string");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
    }

    // Run all tests
    test_none_literal();
    test_bool_literal();
    test_integer_literal();
    test_float_literal();
    test_decimal_literal();
    test_string_literal();
    test_duration_literal();
    test_unary_not();
    test_unary_negate();
    test_unary_positive();
    test_binary_add();
    test_binary_add_float();
    test_binary_subtract();
    test_binary_multiply();
    test_binary_divide();
    test_binary_modulo();
    test_binary_comparison();
    test_binary_equality();
    test_binary_logical();
    test_binary_divide_by_zero();
    test_path_input();
    test_path_ctx();
    test_path_node_output();
    test_path_unresolved();
    test_struct_literal();
    test_list_literal();
    test_member_access();
    test_list_length();
    test_index_access();
    test_index_out_of_bounds();
    test_some_expr();
    test_qualified_value();
    test_group_expr();
    test_call_expr_error();
    test_print_value();

    std::cout << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}

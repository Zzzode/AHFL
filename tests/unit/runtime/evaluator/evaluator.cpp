#include "runtime/evaluator/evaluator.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/builtin_hooks.hpp"
#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/value.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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
    return Expr{std::move(node), {}, {}};
}

ExprArena &test_expr_arena() {
    static ExprArena arena;
    return arena;
}

ExprRef make_expr_ptr(ExprNode node) {
    return test_expr_arena().make(std::move(node));
}

StatementPtr make_stmt_ptr(StatementNode node) {
    return std::make_unique<Statement>(Statement{std::move(node), {}});
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

    auto quoted = eval_expr(make_expr(StringLiteralExpr{"\"hello\\nworld\""}), ctx);
    check(!quoted.has_errors(), "string_literal_quoted.no_error");
    auto *qv = std::get_if<StringValue>(&quoted.value.node);
    check(qv != nullptr && qv->value == "hello\nworld", "string_literal_quoted.value");
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
    auto expr = make_expr(UnaryExpr{ExprUnaryOp::Positive, make_expr_ptr(IntegerLiteralExpr{"7"})});
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
    std::vector<ExprRef> items;
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

// ============================================================================
// Error Cases
// ============================================================================

void test_call_expr_error() {
    EvalContext ctx;
    auto expr = make_expr(CallExpr{"some_function", {}});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "call_expr.has_error");
}

void test_std_builtin_allowlist_registered_in_runtime() {
    const auto &table = BuiltinTable::instance();
    const auto names = table.names();
    check(std::is_sorted(names.begin(), names.end()), "std.builtin_table.names_sorted");

    for (const auto hook : ahfl::known_builtin_hooks()) {
        const std::string hook_name{hook};
        check(table.find(hook) != nullptr, "std.builtin_table.find." + hook_name);
        check(std::find(names.begin(), names.end(), hook_name) != names.end(),
              "std.builtin_table.names." + hook_name);
    }
}

void test_std_builtin_hooks() {
    EvalContext ctx;

    std::vector<ExprRef> list_items;
    list_items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    list_items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    auto list_length = make_expr(CallExpr{
        "list_raw_length",
        {make_expr_ptr(ListLiteralExpr{std::move(list_items)})},
    });
    auto length_result = eval_expr(list_length, ctx);
    check(!length_result.has_errors(), "std.list_raw_length.no_error");
    auto *length = std::get_if<IntValue>(&length_result.value.node);
    check(length != nullptr && length->value == 2, "std.list_raw_length.value");

    std::vector<ExprRef> get_items;
    get_items.push_back(make_expr_ptr(IntegerLiteralExpr{"4"}));
    get_items.push_back(make_expr_ptr(IntegerLiteralExpr{"5"}));
    auto list_get = make_expr(CallExpr{
        "list_raw_get",
        {make_expr_ptr(ListLiteralExpr{std::move(get_items)}),
         make_expr_ptr(IntegerLiteralExpr{"1"})},
    });
    auto get_result = eval_expr(list_get, ctx);
    check(!get_result.has_errors(), "std.list_raw_get.no_error");
    auto *list_value = std::get_if<IntValue>(&get_result.value.node);
    check(list_value != nullptr && list_value->value == 5, "std.list_raw_get.value");

    std::vector<MapEntryExpr> map_entries;
    map_entries.push_back(MapEntryExpr{make_expr_ptr(StringLiteralExpr{"a"}),
                                       make_expr_ptr(IntegerLiteralExpr{"10"})});
    map_entries.push_back(MapEntryExpr{make_expr_ptr(StringLiteralExpr{"b"}),
                                       make_expr_ptr(IntegerLiteralExpr{"20"})});
    auto map_get = make_expr(CallExpr{
        "map_raw_get",
        {make_expr_ptr(MapLiteralExpr{std::move(map_entries)}),
         make_expr_ptr(StringLiteralExpr{"b"})},
    });
    auto map_result = eval_expr(map_get, ctx);
    check(!map_result.has_errors(), "std.map_raw_get.no_error");
    auto *map_value = std::get_if<IntValue>(&map_result.value.node);
    check(map_value != nullptr && map_value->value == 20, "std.map_raw_get.value");

    auto string_contains = make_expr(CallExpr{
        "string_contains",
        {make_expr_ptr(StringLiteralExpr{"abcdef"}), make_expr_ptr(StringLiteralExpr{"cd"})},
    });
    auto contains_result = eval_expr(string_contains, ctx);
    check(!contains_result.has_errors(), "std.string_contains.no_error");
    auto *contains = std::get_if<BoolValue>(&contains_result.value.node);
    check(contains != nullptr && contains->value, "std.string_contains.value");

    auto option_some = make_expr(CallExpr{
        "std::option::Option::Some",
        {make_expr_ptr(IntegerLiteralExpr{"99"})},
    });
    auto option_some_result = eval_expr(option_some, ctx);
    check(!option_some_result.has_errors(), "std.option_constructor_some.no_error");
    auto *some_value = std::get_if<OptionalValue>(&option_some_result.value.node);
    check(some_value != nullptr && some_value->inner != nullptr,
          "std.option_constructor_some.some");
    if (some_value != nullptr && some_value->inner != nullptr) {
        auto *value = std::get_if<IntValue>(&some_value->inner->node);
        check(value != nullptr && value->value == 99, "std.option_constructor_some.value");
    }

    auto option_none = make_expr(CallExpr{
        "std::option::Option::None",
        {},
    });
    auto option_none_result = eval_expr(option_none, ctx);
    check(!option_none_result.has_errors(), "std.option_constructor_none.no_error");
    auto *none_value = std::get_if<OptionalValue>(&option_none_result.value.node);
    check(none_value != nullptr && none_value->inner == nullptr,
          "std.option_constructor_none.none");

    auto result_ok = make_expr(CallExpr{
        "std::result::Result::Ok",
        {make_expr_ptr(IntegerLiteralExpr{"123"})},
    });
    auto result_ok_value = eval_expr(result_ok, ctx);
    check(!result_ok_value.has_errors(), "std.result_constructor_ok.no_error");
    auto *ok_enum = std::get_if<EnumValue>(&result_ok_value.value.node);
    check(ok_enum != nullptr && ok_enum->enum_name == "std::result::Result" &&
              ok_enum->variant == "Ok" && ok_enum->payload.size() == 1,
          "std.result_constructor_ok.value");
    if (ok_enum != nullptr && ok_enum->payload.size() == 1 && ok_enum->payload.front()) {
        auto *payload = std::get_if<IntValue>(&ok_enum->payload.front()->node);
        check(payload != nullptr && payload->value == 123, "std.result_constructor_ok.payload");
    }

    ctx.bind_local("t", make_timestamp(1000));
    ctx.bind_local("delta", make_duration("2s"));
    Path time_path;
    time_path.root_name = "t";
    Path delta_path;
    delta_path.root_name = "delta";
    auto time_add = make_expr(CallExpr{
        "time_add",
        {make_expr_ptr(PathExpr{time_path}), make_expr_ptr(PathExpr{delta_path})},
    });
    auto time_result = eval_expr(time_add, ctx);
    check(!time_result.has_errors(), "std.time_add.no_error");
    auto *timestamp = std::get_if<TimestampValue>(&time_result.value.node);
    check(timestamp != nullptr && timestamp->unix_ms == 3000, "std.time_add.value");

    auto uuid_value = make_uuid("550e8400-e29b-41d4-a716-446655440000");
    check(uuid_value.has_value(), "std.uuid.seed_valid");
    if (uuid_value.has_value()) {
        ctx.bind_local("id", std::move(*uuid_value));
        Path id_path;
        id_path.root_name = "id";
        auto uuid_to_string = make_expr(CallExpr{
            "uuid_to_string",
            {make_expr_ptr(PathExpr{id_path})},
        });
        auto uuid_string_result = eval_expr(uuid_to_string, ctx);
        check(!uuid_string_result.has_errors(), "std.uuid_to_string.no_error");
        auto *uuid_string = std::get_if<StringValue>(&uuid_string_result.value.node);
        check(uuid_string != nullptr && uuid_string->value == "550e8400e29b41d4a716446655440000",
              "std.uuid_to_string.value");
    }
}

Path local_path(std::string name) {
    Path path;
    path.root_name = std::move(name);
    return path;
}

ExprRef int_literal(std::string spelling) {
    return make_expr_ptr(IntegerLiteralExpr{std::move(spelling)});
}

ExprRef local_expr(std::string name) {
    return make_expr_ptr(PathExpr{local_path(std::move(name))});
}

void test_runtime_callable_calls() {
    EvalContext ctx;
    ctx.bind_local("offset", make_int(10));

    auto increment_value = eval_expr(make_expr(LambdaExpr{{"x"},
                                                          make_expr_ptr(BinaryExpr{
                                                              ExprBinaryOp::Add,
                                                              local_expr("x"),
                                                              int_literal("1"),
                                                          })}),
                                     ctx);
    check(!increment_value.has_errors(), "runtime.lambda_value.no_error");
    ctx.bind_local("f", std::move(increment_value.value));
    auto direct_callable_call = make_expr(CallExpr{"f", {int_literal("4")}});
    auto direct_callable_result = eval_expr(direct_callable_call, ctx);
    check(!direct_callable_result.has_errors(), "runtime.callable_call.no_error");
    auto *direct_callable_value = std::get_if<IntValue>(&direct_callable_result.value.node);
    check(direct_callable_value != nullptr && direct_callable_value->value == 5,
          "runtime.callable_call.value");

    auto captured_value = eval_expr(make_expr(LambdaExpr{{"x"},
                                                         make_expr_ptr(BinaryExpr{
                                                             ExprBinaryOp::Add,
                                                             local_expr("x"),
                                                             local_expr("offset"),
                                                         })}),
                                    ctx);
    check(!captured_value.has_errors(), "runtime.lambda_capture.no_error");
    ctx.bind_local("captured", std::move(captured_value.value));
    ctx.bind_local("offset", make_int(99));
    auto captured_callable_call = make_expr(CallExpr{"captured", {int_literal("5")}});
    auto captured_callable_result = eval_expr(captured_callable_call, ctx);
    check(!captured_callable_result.has_errors(), "runtime.callable_capture.no_error");
    auto *captured_callable_value = std::get_if<IntValue>(&captured_callable_result.value.node);
    check(captured_callable_value != nullptr && captured_callable_value->value == 15,
          "runtime.callable_capture.value");
}

void test_match_expr_result_payload_binding() {
    MatchExpr match_expr;
    match_expr.scrutinee = make_expr_ptr(CallExpr{"std::result::Result::Ok", {int_literal("41")}});

    VariantPattern ok_variant;
    ok_variant.path = "Ok";
    ok_variant.subpatterns.push_back(make_owned<MatchPattern>(MatchPattern{
        .node = BindingPattern{.name = "value"},
        .source_range = std::nullopt,
        .text = "value",
    }));
    MatchArmExpr ok_arm;
    ok_arm.pattern = MatchPattern{
        .node = std::move(ok_variant),
        .source_range = std::nullopt,
        .text = "Ok(value)",
    };
    ok_arm.body = make_expr_ptr(BinaryExpr{
        ExprBinaryOp::Add,
        local_expr("value"),
        int_literal("1"),
    });
    match_expr.arms.push_back(std::move(ok_arm));

    VariantPattern err_variant;
    err_variant.path = "Err";
    err_variant.subpatterns.push_back(make_owned<MatchPattern>(MatchPattern{
        .node = WildcardPattern{},
        .source_range = std::nullopt,
        .text = "_",
    }));
    MatchArmExpr err_arm;
    err_arm.pattern = MatchPattern{
        .node = std::move(err_variant),
        .source_range = std::nullopt,
        .text = "Err(_)",
    };
    err_arm.body = int_literal("0");
    match_expr.arms.push_back(std::move(err_arm));

    EvalContext ctx;
    auto result = eval_expr(make_expr(std::move(match_expr)), ctx);
    check(!result.has_errors(), "match_expr.result_payload.no_error");
    auto *value = std::get_if<IntValue>(&result.value.node);
    check(value != nullptr && value->value == 42, "match_expr.result_payload.value");
}

void test_program_function_call_eval() {
    Program program;

    Block add_one_body;
    add_one_body.statements.push_back(make_stmt_ptr(ReturnStatement{
        make_expr_ptr(BinaryExpr{
            ExprBinaryOp::Add,
            local_expr("x"),
            int_literal("1"),
        }),
    }));
    FnDecl add_one;
    add_one.name = "add_one";
    add_one.params.push_back(ParamDecl{.name = "x"});
    add_one.has_body = true;
    add_one.body = make_owned<Block>(std::move(add_one_body));
    program.declarations.push_back(std::move(add_one));

    Block add_two_body;
    add_two_body.statements.push_back(make_stmt_ptr(ReturnStatement{
        make_expr_ptr(CallExpr{
            "add_one",
            {make_expr_ptr(CallExpr{"add_one", {local_expr("x")}})},
        }),
    }));
    FnDecl add_two;
    add_two.name = "add_two";
    add_two.params.push_back(ParamDecl{.name = "x"});
    add_two.has_body = true;
    add_two.body = make_owned<Block>(std::move(add_two_body));
    program.declarations.push_back(std::move(add_two));

    Block apply_body;
    apply_body.statements.push_back(make_stmt_ptr(ReturnStatement{
        make_expr_ptr(CallExpr{
            "f",
            {local_expr("x")},
        }),
    }));
    FnDecl apply;
    apply.name = "apply";
    apply.params.push_back(ParamDecl{.name = "f"});
    apply.params.push_back(ParamDecl{.name = "x"});
    apply.has_body = true;
    apply.body = make_owned<Block>(std::move(apply_body));
    program.declarations.push_back(std::move(apply));

    EvalContext ctx;
    auto call_eval = make_program_call_eval(program);

    auto add_two_call = make_expr(CallExpr{"add_two", {int_literal("40")}});
    auto add_two_result = eval_expr(add_two_call, ctx, call_eval);
    check(!add_two_result.has_errors(), "program_call.add_two.no_error");
    auto *add_two_value = std::get_if<IntValue>(&add_two_result.value.node);
    check(add_two_value != nullptr && add_two_value->value == 42, "program_call.add_two.value");

    auto callback_call = make_expr(CallExpr{
        "apply",
        {make_expr_ptr(LambdaExpr{
             {"y"},
             make_expr_ptr(CallExpr{"add_one", {local_expr("y")}}),
         }),
         int_literal("41")},
    });
    auto callback_result = eval_expr(callback_call, ctx, call_eval);
    check(!callback_result.has_errors(), "program_call.callable_callback.no_error");
    auto *callback_value = std::get_if<IntValue>(&callback_result.value.node);
    check(callback_value != nullptr && callback_value->value == 42,
          "program_call.callable_callback.value");

    auto builtin_call =
        make_expr(CallExpr{"string_length", {make_expr_ptr(StringLiteralExpr{"abcd"})}});
    auto builtin_result = eval_expr(builtin_call, ctx, call_eval);
    check(!builtin_result.has_errors(), "program_call.builtin_fallback.no_error");
    auto *builtin_value = std::get_if<IntValue>(&builtin_result.value.node);
    check(builtin_value != nullptr && builtin_value->value == 4,
          "program_call.builtin_fallback.value");
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
    test_call_expr_error();
    test_std_builtin_allowlist_registered_in_runtime();
    test_std_builtin_hooks();
    test_runtime_callable_calls();
    test_match_expr_result_payload_binding();
    test_program_function_call_eval();
    test_print_value();

    std::cout << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}

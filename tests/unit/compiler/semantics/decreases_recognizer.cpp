#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/decreases_recognizer.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::test_decreases {

using sema::DecreasePatternKind;
using sema::LexOrder;
using sema::LexOrderEntry;
using sema::PatternLengthSelf;
using sema::PatternMinusOne;
using sema::PatternSelfField;
using sema::PatternWildcard;
using sema::RecognizerContext;
using sema::recognize_decreases;
using sema::recognize_single;
using sema::to_string;

namespace {

// ---------------------------------------------------------------------------
// AST construction helpers.
// ---------------------------------------------------------------------------

ahfl::SourceRange const kZeroRange = ahfl::SourceRange{.begin_offset = 0, .end_offset = 0};

Owned<ast::ExprSyntax> make_path(std::string_view root,
                                 std::vector<std::string> members = {},
                                 ast::PathRootKind root_kind = ast::PathRootKind::Identifier) {
    auto path = make_owned<ast::PathSyntax>();
    path->range = kZeroRange;
    path->root_kind = root_kind;
    path->root_name = std::string{root};
    path->members = std::move(members);
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->node = ast::PathExpr{.path = std::move(path)};
    return expr;
}

Owned<ast::ExprSyntax> make_ident(std::string_view name) {
    return make_path(name);
}

// Constructs `length(self)` CallExpr.
Owned<ast::ExprSyntax> make_length_self() {
    auto callee = make_owned<ast::QualifiedName>();
    callee->range = kZeroRange;
    callee->segments = {"length"};
    std::vector<Owned<ast::ExprSyntax>> args;
    args.push_back(make_path("self"));
    auto call_node = ast::CallExpr{
        .callee = std::move(callee),
        .arguments = std::move(args),
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = "length(self)";
    expr->node = std::move(call_node);
    return expr;
}

// Constructs `self.<field>` as MemberAccessExpr.
Owned<ast::ExprSyntax> make_self_field(std::string_view field) {
    auto base = make_path("self");
    auto ma = ast::MemberAccessExpr{
        .base = std::move(base),
        .member = std::string{field},
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = std::string("self.") + std::string{field};
    expr->node = std::move(ma);
    return expr;
}

// Constructs `<ident> - 1` BinaryExpr.
Owned<ast::ExprSyntax> make_minus_one(std::string_view ident) {
    auto lhs = make_ident(ident);
    auto int_node = make_owned<ast::IntegerSyntax>();
    int_node->range = kZeroRange;
    int_node->spelling = "1";
    int_node->value = 1;
    auto rhs = make_owned<ast::ExprSyntax>();
    rhs->range = kZeroRange;
    rhs->text = "1";
    rhs->node = ast::IntegerLiteralExpr{.literal = std::move(int_node)};
    auto bin = ast::BinaryExpr{
        .op = ast::ExprBinaryOp::Subtract,
        .lhs = std::move(lhs),
        .rhs = std::move(rhs),
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = std::string{ident} + std::string{" - 1"};
    expr->node = std::move(bin);
    return expr;
}

// Constructs an integer literal expression.
Owned<ast::ExprSyntax> make_int_lit(std::int64_t value) {
    auto int_node = make_owned<ast::IntegerSyntax>();
    int_node->range = kZeroRange;
    int_node->spelling = std::to_string(value);
    int_node->value = value;
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = int_node->spelling;
    expr->node = ast::IntegerLiteralExpr{.literal = std::move(int_node)};
    return expr;
}

// Moves a list of Owned<ExprSyntax> into a vector<ExprSyntax> for the recognizer.
std::vector<ast::ExprSyntax> move_exprs(std::initializer_list<Owned<ast::ExprSyntax>> owned_exprs) {
    std::vector<ast::ExprSyntax> result;
    result.reserve(owned_exprs.size());
    for (auto const &owned : owned_exprs) {
        result.push_back(std::move(*owned));
    }
    return result;
}

}  // namespace

// ============================================================================
// Golden 1: length(self)
// ============================================================================

TEST_CASE("decreases.golden.01_length_self") {
    INFO("Recognize length(self) as LengthSelf pattern");
    auto exprs = move_exprs({make_length_self()});
    LexOrderEntry entry = recognize_single(exprs.front(), 0);
    REQUIRE(entry.kind == DecreasePatternKind::LengthSelf);
    auto const &detail = std::get<PatternLengthSelf>(entry.detail);
    REQUIRE(detail.subterm_index == 0);
}

TEST_CASE("decreases.golden.01_length_self_negative") {
    INFO("Negative: length(x) is not LengthSelf");
    auto callee = make_owned<ast::QualifiedName>();
    callee->range = kZeroRange;
    callee->segments = {"length"};
    std::vector<Owned<ast::ExprSyntax>> args;
    args.push_back(make_ident("x"));
    auto call_node = ast::CallExpr{
        .callee = std::move(callee),
        .arguments = std::move(args),
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = "length(x)";
    expr->node = std::move(call_node);
    LexOrderEntry entry = recognize_single(*expr, 0);
    CHECK(entry.kind == DecreasePatternKind::Unknown);

    INFO("Negative: len(self) is not LengthSelf (wrong callee name)");
    auto callee2 = make_owned<ast::QualifiedName>();
    callee2->range = kZeroRange;
    callee2->segments = {"len"};
    std::vector<Owned<ast::ExprSyntax>> args2;
    args2.push_back(make_path("self"));
    auto call_node2 = ast::CallExpr{
        .callee = std::move(callee2),
        .arguments = std::move(args2),
    };
    auto expr2 = make_owned<ast::ExprSyntax>();
    expr2->range = kZeroRange;
    expr2->text = "len(self)";
    expr2->node = std::move(call_node2);
    LexOrderEntry entry2 = recognize_single(*expr2, 0);
    CHECK(entry2.kind == DecreasePatternKind::Unknown);
}

// ============================================================================
// Golden 2: self.<field>
// ============================================================================

TEST_CASE("decreases.golden.02_self_field_member_access_shape") {
    INFO("Recognize self.idx via MemberAccessExpr shape");
    auto exprs = move_exprs({make_self_field("idx")});
    LexOrderEntry entry = recognize_single(exprs.front(), 0);
    REQUIRE(entry.kind == DecreasePatternKind::SelfField);
    auto const &detail = std::get<PatternSelfField>(entry.detail);
    REQUIRE(detail.field_name == "idx");
    REQUIRE(detail.subterm_index == 0);
}

TEST_CASE("decreases.golden.02_self_field_path_shape") {
    INFO("Recognize self.iterations via PathExpr shape (root=self + 1 member)");
    auto expr = make_path("self", {"iterations"});
    LexOrderEntry entry = recognize_single(*expr, 0);
    CHECK(entry.kind == DecreasePatternKind::SelfField);
    auto const &detail = std::get<PatternSelfField>(entry.detail);
    CHECK(detail.field_name == "iterations");
}

TEST_CASE("decreases.golden.02_self_field_negative") {
    INFO("Negative: x.idx is not SelfField (root is not self)");
    auto base = make_ident("x");
    auto ma = ast::MemberAccessExpr{
        .base = std::move(base),
        .member = "idx",
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = "x.idx";
    expr->node = std::move(ma);
    LexOrderEntry entry = recognize_single(*expr, 0);
    CHECK(entry.kind == DecreasePatternKind::Unknown);
}

// ============================================================================
// Golden 3: <ident> - 1
// ============================================================================

TEST_CASE("decreases.golden.03_minus_one_basic") {
    INFO("Recognize x - 1");
    auto exprs = move_exprs({make_minus_one("x")});
    LexOrderEntry entry = recognize_single(exprs.front(), 0);
    REQUIRE(entry.kind == DecreasePatternKind::MinusOne);
    auto const &detail = std::get<PatternMinusOne>(entry.detail);
    REQUIRE(detail.identifier == "x");
    REQUIRE(detail.subterm_index == 0);
}

TEST_CASE("decreases.golden.03_minus_one_various_name") {
    INFO("Recognize counter - 1");
    auto exprs = move_exprs({make_minus_one("counter")});
    LexOrderEntry entry = recognize_single(exprs.front(), 0);
    CHECK(entry.kind == DecreasePatternKind::MinusOne);
    CHECK(std::get<PatternMinusOne>(entry.detail).identifier == "counter");
}

TEST_CASE("decreases.golden.03_minus_one_negatives") {
    INFO("Negative: x - 2 (not value 1)");
    auto lhs = make_ident("x");
    auto int_node = make_owned<ast::IntegerSyntax>();
    int_node->range = kZeroRange;
    int_node->spelling = "2";
    int_node->value = 2;
    auto rhs = make_owned<ast::ExprSyntax>();
    rhs->range = kZeroRange;
    rhs->text = "2";
    rhs->node = ast::IntegerLiteralExpr{.literal = std::move(int_node)};
    auto bin = ast::BinaryExpr{
        .op = ast::ExprBinaryOp::Subtract,
        .lhs = std::move(lhs),
        .rhs = std::move(rhs),
    };
    auto expr = make_owned<ast::ExprSyntax>();
    expr->range = kZeroRange;
    expr->text = "x - 2";
    expr->node = std::move(bin);
    LexOrderEntry entry = recognize_single(*expr, 0);
    CHECK(entry.kind == DecreasePatternKind::Unknown);

    INFO("Negative: x + 1 (wrong operator)");
    auto lhs2 = make_ident("x");
    auto int_node2 = make_owned<ast::IntegerSyntax>();
    int_node2->range = kZeroRange;
    int_node2->spelling = "1";
    int_node2->value = 1;
    auto rhs2 = make_owned<ast::ExprSyntax>();
    rhs2->range = kZeroRange;
    rhs2->text = "1";
    rhs2->node = ast::IntegerLiteralExpr{.literal = std::move(int_node2)};
    auto bin2 = ast::BinaryExpr{
        .op = ast::ExprBinaryOp::Add,
        .lhs = std::move(lhs2),
        .rhs = std::move(rhs2),
    };
    auto expr2 = make_owned<ast::ExprSyntax>();
    expr2->range = kZeroRange;
    expr2->text = "x + 1";
    expr2->node = std::move(bin2);
    LexOrderEntry entry2 = recognize_single(*expr2, 0);
    CHECK(entry2.kind == DecreasePatternKind::Unknown);

    INFO("Negative: 5 - 1 (literal on LHS, not identifier)");
    auto lhs3 = make_int_lit(5);
    auto int_node3 = make_owned<ast::IntegerSyntax>();
    int_node3->range = kZeroRange;
    int_node3->spelling = "1";
    int_node3->value = 1;
    auto rhs3 = make_owned<ast::ExprSyntax>();
    rhs3->range = kZeroRange;
    rhs3->text = "1";
    rhs3->node = ast::IntegerLiteralExpr{.literal = std::move(int_node3)};
    auto bin3 = ast::BinaryExpr{
        .op = ast::ExprBinaryOp::Subtract,
        .lhs = std::move(lhs3),
        .rhs = std::move(rhs3),
    };
    auto expr3 = make_owned<ast::ExprSyntax>();
    expr3->range = kZeroRange;
    expr3->text = "5 - 1";
    expr3->node = std::move(bin3);
    LexOrderEntry entry3 = recognize_single(*expr3, 0);
    CHECK(entry3.kind == DecreasePatternKind::Unknown);
}

// ============================================================================
// Golden 4: wildcard *  (positive + 2 negatives)
// ============================================================================

TEST_CASE("decreases.golden.04_wildcard_nonrec_noloop_allowed") {
    INFO("Wildcard * in non-recursive, loop-less context: valid");
    auto exprs = move_exprs({make_path("*")});
    RecognizerContext ctx{.is_recursive = false, .contains_loop = false};
    LexOrder order = recognize_decreases(exprs, ctx);
    REQUIRE(order.entries.size() == 1);
    REQUIRE(order.entries.front().kind == DecreasePatternKind::Wildcard);
    CHECK(order.has_wildcard);
    CHECK_FALSE(order.wildcard_illegal);
    CHECK(order.fully_recognized());
}

TEST_CASE("decreases.golden.04_wildcard_negative_recursive") {
    INFO("Wildcard * in recursive context: wildcard_illegal == true (negative 1)");
    auto exprs = move_exprs({make_path("*")});
    RecognizerContext ctx{.is_recursive = true, .contains_loop = false};
    LexOrder order = recognize_decreases(exprs, ctx);
    REQUIRE(order.has_wildcard);
    CHECK(order.wildcard_illegal);
    CHECK_FALSE(order.fully_recognized());
}

TEST_CASE("decreases.golden.04_wildcard_negative_loop") {
    INFO("Wildcard * in loopful context: wildcard_illegal == true (negative 2)");
    auto exprs = move_exprs({make_path("*")});
    RecognizerContext ctx{.is_recursive = false, .contains_loop = true};
    LexOrder order = recognize_decreases(exprs, ctx);
    REQUIRE(order.has_wildcard);
    CHECK(order.wildcard_illegal);
    CHECK_FALSE(order.fully_recognized());
}

// ============================================================================
// Golden 5: comprehensive lexicographic order
// ============================================================================

TEST_CASE("decreases.golden.05_lexicographic_combined") {
    INFO("Combined lex: length(self), self.idx, n - 1, and an unknown item");
    auto exprs = move_exprs({
        make_length_self(),
        make_self_field("idx"),
        make_minus_one("n"),
        make_int_lit(42),
    });
    LexOrder order = recognize_decreases(exprs);
    REQUIRE(order.entries.size() == 4);
    CHECK(order.entries[0].kind == DecreasePatternKind::LengthSelf);
    CHECK(order.entries[1].kind == DecreasePatternKind::SelfField);
    CHECK(std::get<PatternSelfField>(order.entries[1].detail).field_name == "idx");
    CHECK(order.entries[2].kind == DecreasePatternKind::MinusOne);
    CHECK(std::get<PatternMinusOne>(order.entries[2].detail).identifier == "n");
    CHECK(order.entries[3].kind == DecreasePatternKind::Unknown);
    CHECK(order.recognized_count() == 3);
    CHECK_FALSE(order.fully_recognized());

    INFO("Fully recognized composite: length(self), self.step, counter-1");
    auto exprs2 = move_exprs({
        make_length_self(),
        make_self_field("step"),
        make_minus_one("counter"),
    });
    LexOrder order2 = recognize_decreases(exprs2);
    CHECK(order2.recognized_count() == 3);
    CHECK(order2.fully_recognized());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("decreases.edge.empty_list") {
    INFO("Empty decreases clause");
    std::vector<ast::ExprSyntax> empty;
    LexOrder order = recognize_decreases(empty);
    CHECK(order.entries.empty());
    CHECK_FALSE(order.fully_recognized());
    CHECK(order.recognized_count() == 0);
    CHECK_FALSE(order.has_wildcard);
}

TEST_CASE("decreases.edge.to_string_values") {
    CHECK(std::string{to_string(DecreasePatternKind::LengthSelf)} == "LengthSelf");
    CHECK(std::string{to_string(DecreasePatternKind::SelfField)} == "SelfField");
    CHECK(std::string{to_string(DecreasePatternKind::MinusOne)} == "MinusOne");
    CHECK(std::string{to_string(DecreasePatternKind::Wildcard)} == "Wildcard");
    CHECK(std::string{to_string(DecreasePatternKind::Unknown)} == "Unknown");
}

}  // namespace ahfl::test_decreases

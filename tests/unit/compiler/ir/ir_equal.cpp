#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/ir_equal.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ahfl::ir;

// Build `input.field <op> <int>` style binary expressions in a shared arena so
// two independently-lowered-but-structurally-identical trees can be compared.
[[nodiscard]] ExprRef make_int(ExprArena &arena, std::string spelling) {
    return arena.make(IntegerLiteralExpr{.spelling = std::move(spelling)});
}

[[nodiscard]] ExprRef make_path(ExprArena &arena, std::string root, std::vector<std::string> members) {
    Path path;
    path.root_kind = PathRootKind::Input;
    path.root_name = std::move(root);
    path.members = std::move(members);
    return arena.make(PathExpr{.path = std::move(path)});
}

[[nodiscard]] ExprRef make_binary(ExprArena &arena, ExprBinaryOp op, ExprRef lhs, ExprRef rhs) {
    return arena.make(BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

} // namespace

TEST_CASE("ir structural equality: identical trees in different slots compare equal") {
    ExprArena arena;
    // a: input.x > 0
    const auto a = make_binary(arena,
                               ExprBinaryOp::Greater,
                               make_path(arena, "input", {"x"}),
                               make_int(arena, "0"));
    // b: input.x > 0 — freshly lowered into different arena slots.
    const auto b = make_binary(arena,
                               ExprBinaryOp::Greater,
                               make_path(arena, "input", {"x"}),
                               make_int(arena, "0"));
    CHECK(a.get() != b.get()); // genuinely distinct nodes
    CHECK(exprs_structurally_equal(a, b));
}

TEST_CASE("ir structural equality: differing operator is unequal") {
    ExprArena arena;
    const auto a = make_binary(arena,
                               ExprBinaryOp::Greater,
                               make_path(arena, "input", {"x"}),
                               make_int(arena, "0"));
    const auto b = make_binary(arena,
                               ExprBinaryOp::Less,
                               make_path(arena, "input", {"x"}),
                               make_int(arena, "0"));
    CHECK_FALSE(exprs_structurally_equal(a, b));
}

TEST_CASE("ir structural equality: differing operand payload is unequal") {
    ExprArena arena;
    const auto a = make_binary(arena,
                               ExprBinaryOp::Greater,
                               make_path(arena, "input", {"x"}),
                               make_int(arena, "0"));
    const auto b = make_binary(arena,
                               ExprBinaryOp::Greater,
                               make_path(arena, "input", {"y"}),
                               make_int(arena, "0"));
    CHECK_FALSE(exprs_structurally_equal(a, b));
}

TEST_CASE("ir structural equality: nested member access recurses") {
    ExprArena arena;
    const auto base_a = make_path(arena, "input", {});
    const auto a = arena.make(MemberAccessExpr{.base = base_a, .member = "field"});
    const auto base_b = make_path(arena, "input", {});
    const auto b = arena.make(MemberAccessExpr{.base = base_b, .member = "field"});
    CHECK(exprs_structurally_equal(a, b));

    const auto c = arena.make(MemberAccessExpr{.base = base_b, .member = "other"});
    CHECK_FALSE(exprs_structurally_equal(a, c));
}

TEST_CASE("ir structural equality: absent refs") {
    ExprArena arena;
    const ExprRef null_ref{nullptr};
    CHECK(exprs_structurally_equal(null_ref, null_ref));
    const auto present = make_int(arena, "1");
    CHECK_FALSE(exprs_structurally_equal(null_ref, present));
    CHECK_FALSE(exprs_structurally_equal(present, null_ref));
}

TEST_CASE("ir structural equality: temporal trees compare structurally") {
    ExprArena arena;
    auto make_completed = []() {
        auto expr = std::make_unique<TemporalExpr>();
        expr->node = CompletedTemporalExpr{.node = "run", .state_name = std::string{"Done"}};
        return expr;
    };
    const auto a = make_completed();
    const auto b = make_completed();
    CHECK(temporal_exprs_structurally_equal(*a, *b));

    auto c = std::make_unique<TemporalExpr>();
    c->node = CompletedTemporalExpr{.node = "run", .state_name = std::nullopt};
    CHECK_FALSE(temporal_exprs_structurally_equal(*a, *c));

    // always(a) vs always(b) — recurse through the unary operand.
    auto unary_a = std::make_unique<TemporalExpr>();
    unary_a->node = TemporalUnaryExpr{.op = TemporalUnaryOp::Always, .operand = make_completed()};
    auto unary_b = std::make_unique<TemporalExpr>();
    unary_b->node = TemporalUnaryExpr{.op = TemporalUnaryOp::Always, .operand = make_completed()};
    CHECK(temporal_exprs_structurally_equal(*unary_a, *unary_b));
}

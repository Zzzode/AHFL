// RFC 0027 P8 IR SSOT groundwork (KR6.13-G): pins the cardinality AND the
// alternative-index order of every IR node variant.
//
// Cardinality is also static_assert-ed next to each using-declaration (so any
// TU that includes the header fails to compile on drift); this test file
// centralizes the expected numbers and, additionally, locks INDEX STABILITY:
// downstream code relies on `std::variant::index()` / alternative positions,
// so reordering alternatives is a breaking change and must be intentional.
//
// The ExprNode expected order is derived from the PRODUCTION X-macro
// (include/ahfl/compiler/ir/expr_nodes.def) the variant, the ir_json wire table,
// and the negative compile-test all consume — one list, no parallel copy.

#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/program.hpp"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

template <typename T> struct type_tag {
    using type = T;
};

#define HANDLE_EXPR_NODE(Name, Wire) type_tag<ahfl::ir::Name>{},
constexpr auto kExprNodeTags = std::tuple{
#include "ahfl/compiler/ir/expr_nodes.def"
};
#undef HANDLE_EXPR_NODE

// RFC 0027 P6/P7 (KR6.13-E): the production X-macro node list — the SAME
// include/ahfl/compiler/ir/expr_nodes.def the variant and the ir_json wire
// table are generated from — re-expanded here to recover the wire names. If the
// production table and this expansion disagree, the assertion below fails.
#define HANDLE_EXPR_NODE(Name, Wire) Wire,
constexpr std::string_view kExprNodeWireNames[] = {
#include "ahfl/compiler/ir/expr_nodes.def"
};
#undef HANDLE_EXPR_NODE

// Static: the production .def list's wire names are exactly what
// expr_node_wire_name(i) returns, in order — the writer/reader share ONE table.
[[nodiscard]] constexpr bool expr_wire_table_matches() noexcept {
    for (std::size_t i = 0; i < std::variant_size_v<ahfl::ir::ExprNode>; ++i) {
        if (ahfl::ir::expr_node_wire_name(i) != kExprNodeWireNames[i]) {
            return false;
        }
    }
    return true;
}

static_assert(std::size(kExprNodeWireNames) == std::variant_size_v<ahfl::ir::ExprNode>,
              "expr_nodes.def wire-name count must equal ahfl::ir::ExprNode cardinality");
static_assert(expr_wire_table_matches(),
              "ahfl::ir::expr_node_wire_name must equal the expr_nodes.def wire "
              "column in declaration order (single wire-name SSOT)");

// Static: the shared .def list is an exact, ORDER-PRESERVING mirror of
// ahfl::ir::ExprNode. A tuple-position/type mismatch is a compile failure.
template <std::size_t... Is>
[[nodiscard]] constexpr bool
expr_tag_sequence_matches_variant(std::index_sequence<Is...>) noexcept {
    return (std::is_same_v<
                std::tuple_element_t<Is, std::remove_cvref_t<decltype(kExprNodeTags)>>,
                type_tag<std::variant_alternative_t<Is, ahfl::ir::ExprNode>>> &&
            ...);
}

static_assert(std::tuple_size_v<std::remove_cvref_t<decltype(kExprNodeTags)>> ==
                  std::variant_size_v<ahfl::ir::ExprNode>,
              "expr_nodes.def length must equal ahfl::ir::ExprNode cardinality");
static_assert(
    expr_tag_sequence_matches_variant(
        std::make_index_sequence<
            std::variant_size_v<ahfl::ir::ExprNode>>{}),
    "expr_nodes.def order must match ahfl::ir::ExprNode alternative order");

// Static cardinality pins (mirror the header-adjacent static_asserts in one
// auditable place; numbers cross-checked in the RFC 0027 P8 groundwork slice).
static_assert(std::variant_size_v<ahfl::ir::MatchPatternNode> == 7);
static_assert(std::variant_size_v<ahfl::ir::ExprNode> == 20);
static_assert(std::variant_size_v<ahfl::ir::TemporalExprNode> == 7);
static_assert(std::variant_size_v<ahfl::ir::StatementNode> == 11);
static_assert(std::variant_size_v<ahfl::ir::Decl> == 16);
static_assert(std::variant_size_v<ahfl::ir::core::CoreDecl> == 2);
static_assert(std::variant_size_v<ahfl::ir::core::CoreExprNode> == 10);
static_assert(std::variant_size_v<ahfl::ir::core::CorePatternNode> == 7);
static_assert(std::variant_size_v<ahfl::ir::core::CoreStmtNode> == 9);
static_assert(std::variant_size_v<ahfl::ir::core::CoreInstancePayload> == 5);
static_assert(std::variant_size_v<ahfl::ir::core::CoreValueTypeNode> == 14);

// Compile-time ordered-index anchors: alternative T must live at index I.
template <typename V, std::size_t I, typename T>
[[nodiscard]] consteval bool alternative_at_index() noexcept {
    return std::is_same_v<std::variant_alternative_t<I, V>, T>;
}

static_assert(alternative_at_index<ahfl::ir::MatchPatternNode, 0,
                                   ahfl::ir::LiteralPattern>());
static_assert(alternative_at_index<ahfl::ir::MatchPatternNode, 6,
                                   ahfl::ir::OrPattern>());
static_assert(alternative_at_index<ahfl::ir::ExprNode, 0,
                                   ahfl::ir::BoolLiteralExpr>());
static_assert(alternative_at_index<ahfl::ir::ExprNode, 19,
                                   ahfl::ir::QuantifierExpr>());
static_assert(alternative_at_index<ahfl::ir::TemporalExprNode, 0,
                                   ahfl::ir::EmbeddedTemporalExpr>());
static_assert(alternative_at_index<ahfl::ir::TemporalExprNode, 6,
                                   ahfl::ir::TemporalBinaryExpr>());
static_assert(alternative_at_index<ahfl::ir::StatementNode, 0,
                                   ahfl::ir::LetStatement>());
static_assert(alternative_at_index<ahfl::ir::StatementNode, 10,
                                   ahfl::ir::ExprStatement>());
static_assert(alternative_at_index<ahfl::ir::Decl, 0, ahfl::ir::ModuleDecl>());
static_assert(alternative_at_index<ahfl::ir::Decl, 15,
                                   ahfl::ir::InstanceDecl>());

static_assert(alternative_at_index<ahfl::ir::core::CoreDecl, 0,
                                   ahfl::ir::core::CoreAgentDecl>());
static_assert(alternative_at_index<ahfl::ir::core::CoreDecl, 1,
                                   ahfl::ir::core::CoreCapabilityDecl>());
static_assert(alternative_at_index<ahfl::ir::core::CoreExprNode, 0,
                                   ahfl::ir::core::CoreLiteralExpr>());
static_assert(alternative_at_index<ahfl::ir::core::CoreExprNode, 8,
                                   ahfl::ir::core::CoreCollectionExpr>());
static_assert(alternative_at_index<ahfl::ir::core::CoreExprNode, 9,
                                   ahfl::ir::core::CoreUnsupportedExpr>());
static_assert(alternative_at_index<ahfl::ir::core::CorePatternNode, 0,
                                   ahfl::ir::core::CoreWildcardPat>());
static_assert(alternative_at_index<ahfl::ir::core::CorePatternNode, 6,
                                   ahfl::ir::core::CoreOrPat>());
static_assert(alternative_at_index<ahfl::ir::core::CoreStmtNode, 0,
                                   ahfl::ir::core::CoreLetStmt>());
static_assert(alternative_at_index<ahfl::ir::core::CoreStmtNode, 8,
                                   ahfl::ir::core::CoreMatchStmt>());
static_assert(alternative_at_index<ahfl::ir::core::CoreInstancePayload, 0,
                                   ahfl::ir::core::CoreCapabilityInstance>());
static_assert(alternative_at_index<ahfl::ir::core::CoreInstancePayload, 4,
                                   ahfl::ir::core::CoreFnInstance>());
static_assert(alternative_at_index<ahfl::ir::core::CoreValueTypeNode, 0,
                                   ahfl::ir::core::CoreVtUnit>());
static_assert(alternative_at_index<ahfl::ir::core::CoreValueTypeNode, 13,
                                   ahfl::ir::core::CoreVtClosure>());

// Alternative index of T within the shared .def order (== std::variant index).
template <typename T, typename Tuple, std::size_t... Is>
[[nodiscard]] constexpr std::size_t
tag_index_impl(std::index_sequence<Is...>) noexcept {
    std::size_t result = 0;
    ((std::is_same_v<std::tuple_element_t<Is, Tuple>, type_tag<T>> ? void(result = Is)
                                                                    : void()),
     ...);
    return result;
}

template <typename T>
[[nodiscard]] constexpr std::size_t expr_node_index() noexcept {
    return tag_index_impl<T, std::remove_cvref_t<decltype(kExprNodeTags)>>(
        std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(
            kExprNodeTags)>>>{});
}

static_assert(expr_node_index<ahfl::ir::BoolLiteralExpr>() == 0);
static_assert(expr_node_index<ahfl::ir::QuantifierExpr>() == 19);

} // namespace

TEST_CASE("ir node variants: pinned cardinalities") {
    CHECK(std::variant_size_v<ahfl::ir::MatchPatternNode> == 7);
    CHECK(std::variant_size_v<ahfl::ir::ExprNode> == 20);
    CHECK(std::variant_size_v<ahfl::ir::TemporalExprNode> == 7);
    CHECK(std::variant_size_v<ahfl::ir::StatementNode> == 11);
    CHECK(std::variant_size_v<ahfl::ir::Decl> == 16);
    CHECK(std::variant_size_v<ahfl::ir::core::CoreDecl> == 2);
    CHECK(std::variant_size_v<ahfl::ir::core::CoreExprNode> == 10);
    CHECK(std::variant_size_v<ahfl::ir::core::CorePatternNode> == 7);
    CHECK(std::variant_size_v<ahfl::ir::core::CoreStmtNode> == 9);
    CHECK(std::variant_size_v<ahfl::ir::core::CoreInstancePayload> == 5);
    CHECK(std::variant_size_v<ahfl::ir::core::CoreValueTypeNode> == 14);
}

TEST_CASE("ir node variants: runtime variant index stability") {
    SUBCASE("ExprNode anchors") {
        ahfl::ir::ExprNode first{ahfl::ir::BoolLiteralExpr{}};
        ahfl::ir::ExprNode last{ahfl::ir::QuantifierExpr{}};
        CHECK(first.index() == 0);
        CHECK(last.index() == 19);
    }
    SUBCASE("StatementNode anchors") {
        ahfl::ir::StatementNode first{ahfl::ir::LetStatement{}};
        ahfl::ir::StatementNode last{ahfl::ir::ExprStatement{}};
        CHECK(first.index() == 0);
        CHECK(last.index() == 10);
    }
    SUBCASE("CoreValueTypeNode anchors") {
        ahfl::ir::core::CoreValueTypeNode first{ahfl::ir::core::CoreVtUnit{}};
        ahfl::ir::core::CoreValueTypeNode last{ahfl::ir::core::CoreVtClosure{}};
        CHECK(first.index() == 0);
        CHECK(last.index() == 13);
    }
    SUBCASE("CoreStmtNode anchors") {
        ahfl::ir::core::CoreStmtNode first{ahfl::ir::core::CoreLetStmt{}};
        ahfl::ir::core::CoreStmtNode last{ahfl::ir::core::CoreMatchStmt{}};
        CHECK(first.index() == 0);
        CHECK(last.index() == 8);
    }
}

// RFC 0027 P6/P7 (KR6.13-E): the ExprNode wire-name table that both the ir_json
// writer and reader resolve the `"kind"` spelling from is generated from
// expr_nodes.def, so alternative index and wire name stay locked together.
TEST_CASE("ir expr node wire names: single table pins index->name") {
    using ahfl::ir::expr_node_wire_name;
    // Ordered-index anchors: alternative at index I carries wire name W.
    CHECK(expr_node_wire_name(0) == "bool_literal");
    CHECK(expr_node_wire_name(expr_node_index<ahfl::ir::UnitLiteralExpr>()) == "unit_literal");
    CHECK(expr_node_wire_name(19) == "quantifier");
    CHECK(expr_node_wire_name(ahfl::ir::ExprNode{ahfl::ir::CallExpr{}}) == "call");

    // The ExprNodeIndex enum enumerates the same order as the variant.
    using ahfl::ir::expr_node_detail::ExprNodeIndex;
    CHECK(static_cast<std::size_t>(ExprNodeIndex::BoolLiteralExpr) == 0);
    CHECK(static_cast<std::size_t>(ExprNodeIndex::CallExpr) ==
          expr_node_index<ahfl::ir::CallExpr>());
    CHECK(static_cast<std::size_t>(ExprNodeIndex::QuantifierExpr) == 19);
}

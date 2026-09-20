// Negative compile-test fixture for the RFC 0027 P6/P7 (KR6.13-T) DERIVED
// ExprNode child-edge gate. This is the third member of the ssot negative-test
// family (ExprNode exhaustiveness, CoreValueTypeNode exhaustiveness): where
// those pin "adding a node without a visitor handler breaks the build", this one
// pins the sibling property the child-edge derivation introduces — "adding a
// node with children but without an edge entry in expr_nodes.def breaks the
// const/mut traversal build" (the exact silent-skip data-loss class the
// hand-written walks allowed).
//
// tests/scripts/ir_ssot_compile_fail.py syntax-only-compiles this TU once per
// injected direction, plus once clean:
//
//   * clean build (no macro): the production list is complete, so every
//     ahfl::ir::ExprNode alternative has a ChildEdges specialization, the
//     derived walk instantiates for all of them, AND every declared edge list
//     covers its node struct's child-bearing members — compilation MUST succeed.
//   * -DAHFL_SSOT_INJECT_UNHANDLED_CHILD: one sentinel "forgotten" node is
//     appended to the derived variant and then WALKED, exactly as the derived
//     std::visit in visitor.cpp walks every alternative. The sentinel declares
//     no child edges, so naming `ChildEdges<SsotUnhandledChildExpr>::type` MUST
//     fail the build with a diagnostic naming SsotUnhandledChildExpr.
//   * -DAHFL_SSOT_INJECT_LEAF_WITH_CHILDREN: a sentinel whose struct HAS an
//     ExprRef member is wired with `()` (the "leaf placeholder" a contributor
//     reaches for while wiring a node incrementally). The structural coverage
//     gate MUST reject it, naming SsotLeafWithChildrenExpr — this is the
//     silent-skip direction `()` alone could not catch.
//   * -DAHFL_SSOT_INJECT_DUPLICATE_EDGE: a sentinel listing the same ExprRef
//     member twice MUST be rejected, naming SsotDuplicateEdgeExpr (a duplicate
//     would double-visit / re-stamp that child).
//
// The last two directions emit the SAME asserts the production
// HANDLE_EXPR_NODE expansion emits (valid() and child_edges_cover()), applied
// to sentinels, so the fixture pins the gate functions themselves.

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/expr_child_edges.hpp"

namespace ahfl::ir::ssot_child_edges_test {

// Sentinel "node added to the .def without an edge entry".
struct SsotUnhandledChildExpr {};

// Sentinel "node struct HAS a child but its edge list is the leaf `()`": the
// struct shows the gate exactly the field set a node-with-children needs.
struct SsotLeafWithChildrenExpr {
    ExprRef child;
};

// Sentinel "same child member listed twice in one node's edge list".
struct SsotDuplicateEdgeExpr {
    ExprRef first;
    ExprRef second;
};

} // namespace ahfl::ir::ssot_child_edges_test

namespace ahfl::ir::expr_child_detail {

#if defined(AHFL_SSOT_INJECT_LEAF_WITH_CHILDREN)
// Mirror of the production HANDLE_EXPR_NODE expansion for the `()`-on-a-node-
// with-children mistake: valid() passes (there is nothing malformed to reject),
// but child_edges_cover() MUST fail — the struct has an ExprRef member that no
// edge names, which is the silent-skip class this slice removes.
template <> struct ChildEdges<ssot_child_edges_test::SsotLeafWithChildrenExpr> {
    using type = EdgeList<>;
};
static_assert(
    child_edges_cover<ssot_child_edges_test::SsotLeafWithChildrenExpr,
                      ChildEdges<ssot_child_edges_test::SsotLeafWithChildrenExpr>::type>(),
    "SsotLeafWithChildrenExpr: `()` on a node whose struct has an ExprRef member must "
    "be rejected by the structural coverage gate");
#elif defined(AHFL_SSOT_INJECT_DUPLICATE_EDGE)
// `first` twice, `second` never: valid() MUST fail (duplicate member).
template <> struct ChildEdges<ssot_child_edges_test::SsotDuplicateEdgeExpr> {
    using type = EdgeList<BranchEdge<&ssot_child_edges_test::SsotDuplicateEdgeExpr::first>,
                          BranchEdge<&ssot_child_edges_test::SsotDuplicateEdgeExpr::first>>;
};
static_assert(ChildEdges<ssot_child_edges_test::SsotDuplicateEdgeExpr>::type::valid(),
              "SsotDuplicateEdgeExpr: a duplicated child edge must be rejected");
#endif

} // namespace ahfl::ir::expr_child_detail

namespace {

template <typename T> struct type_tag {
    using type = T;
};

#define HANDLE_EXPR_NODE(Name, Wire, Edges) type_tag<ahfl::ir::Name>{},
constexpr auto kFixtureTags = std::tuple{
#include "ahfl/compiler/ir/expr_nodes.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED_CHILD
    type_tag<ahfl::ir::ssot_child_edges_test::SsotUnhandledChildExpr>{}
#endif
};
#undef HANDLE_EXPR_NODE

template <typename Tuple> struct tags_to_variant;
template <typename... Ts> struct tags_to_variant<std::tuple<type_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureExprNode = tags_to_variant<std::remove_cvref_t<decltype(kFixtureTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED_CHILD
// The production .def list still rebuilds the production variant exactly.
static_assert(std::is_same_v<FixtureExprNode, ahfl::ir::ExprNode>,
              "include/ahfl/compiler/ir/expr_nodes.def drifted from "
              "ahfl::ir::ExprNode; the X-list and the generated variant must "
              "stay in lockstep.");
#endif

// A non-template body forces the derived walk to instantiate for EVERY
// alternative of FixtureExprNode — the same instantiation visitor.cpp triggers,
// so a node without a ChildEdges specialization is a hard error naming it.
[[maybe_unused]] bool force_derived_walk(FixtureExprNode &node) {
    auto sink = [](ahfl::ir::Expr &) {
        return true;
    };
    return std::visit(
        [&](auto &alternative) -> bool {
            return ahfl::ir::expr_child_detail::walk_children(alternative, sink);
        },
        node);
}

} // namespace

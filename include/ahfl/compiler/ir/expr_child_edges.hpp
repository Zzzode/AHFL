#pragma once

// RFC 0027 P6/P7 (KR6.13-T): DERIVED ExprNode child-edge traversal.
//
// Before this header, `ProgramVisitor::visit_expr` and
// `ProgramRewriter::rewrite_expr` (src/compiler/ir/visitor.cpp) each carried a
// hand-maintained 20-case enumeration of "which fields of which node are
// children". Adding a node with children therefore required editing BOTH lists
// in the same shape, and a node that missed one was *silently* skipped by that
// walk — invisible data loss, exactly the failure class RFC 0027 §"IR 单一真相
// 源（SSOT）" quotes. Both enumerations are gone: the child edges now live next
// to the node declaration in `expr_nodes.def` (the same X-macro list that
// generates the `ExprNode` variant and the JSON wire-name table) and this header
// derives the traversal from that metadata, for the const and the mutating walk
// alike.
//
// The pre/post/Skip/Abort protocol in visitor.hpp is unchanged — visitor.cpp
// still drives it and only the *edge enumeration* is derived. `walk_children`
// returns `false` as soon as the sink reports "stop", so an `Abort` raised while
// visiting child N short-circuits the remaining edges exactly like the hand
// written loops did.
//
// Edge grammar (the third column of every `HANDLE_EXPR_NODE` line, a
// parenthesized macro-argument list in visit order; `()` means "leaf"):
//
//   * `()`                            — no child edges.
//   * `AHFL_EXPR_BRANCH(&N::m)`       — `m` is an `ExprRef` member of `N`.
//   * `AHFL_EXPR_FANOUT(&N::m)`       — `m` is a `std::vector<ExprRef>` member of
//                                       `N`; every element is a child.
//   * `AHFL_EXPR_PER(&N::m, AHFL_EXPR_ELEM(&E::f), ...)` — `m` is a
//                                       `std::vector<E>` member of `N`; for each
//                                       element in order, each listed `E::f`
//                                       (`ExprRef`) yields a child. The element
//                                       edges are grouped so `match` visits
//                                       `arm.guard` then `arm.body` before the
//                                       next arm, exactly as the hand-written
//                                       per-arm loop did (a flat per-member
//                                       fan-out would reorder the traversal).
//
// Edge order is visit order and therefore part of the contract. `expr_nodes.def`
// defines those spelling macros for the duration of its own expansion and
// `#undef`s them again, so they never leak to a consumer.
//
// Exhaustiveness is a compile-time property here, not a convention:
//   * a node added to `expr_nodes.def` without an edge entry has no
//     `ChildEdges<Name>` specialization, so the static_assert below — and any
//     `walk_children` instantiation for it, which the derived `std::visit` in
//     visitor.cpp reaches for every alternative — is a hard error naming the
//     type;
//   * an edge that names a non-child field fails the per-node
//     `static_assert(EdgeList::valid())` emitted from that same .def line.
// tests/fixtures/ir/ssot/expr_child_edges_negative.cpp pins both properties.

#include "ahfl/compiler/ir/expr.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir::expr_child_detail {

template <typename...> inline constexpr bool always_false = false;

/// Class / value type of a data-member pointer (`R C::*`).
template <typename> struct member_pointer_traits;
template <typename ClassT, typename ValueT> struct member_pointer_traits<ValueT ClassT::*> {
    using class_type = ClassT;
    using value_type = ValueT;
};

/// Element type of a `std::vector<T, Alloc>`.
template <typename> struct vector_element;
template <typename T, typename Alloc> struct vector_element<std::vector<T, Alloc>> {
    using type = T;
};

/// `&N::m` names an `ExprRef` member: one child edge.
template <auto Member> struct BranchEdge {
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, ExprRef>;
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        auto &field = node.*Member;
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(field)>, ExprRef>) {
            return !field || sink(*field);
        } else {
            static_assert(always_false<NodeT>,
                          "AHFL_EXPR_BRANCH names a member that is not an ExprRef; use "
                          "AHFL_EXPR_FANOUT for a std::vector<ExprRef> or AHFL_EXPR_PER "
                          "for a list of element structs.");
            return false;
        }
    }
};

/// `&N::m` names a `std::vector<ExprRef>` member: one child edge per element.
template <auto Member> struct FanoutEdge {
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, std::vector<ExprRef>>;
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(node.*Member)>,
                                     std::vector<ExprRef>>) {
            for (auto &child : node.*Member) {
                if (child && !sink(*child)) {
                    return false;
                }
            }
            return true;
        } else {
            static_assert(always_false<NodeT>,
                          "AHFL_EXPR_FANOUT names a member that is not a "
                          "std::vector<ExprRef>; use AHFL_EXPR_BRANCH for one ExprRef or "
                          "AHFL_EXPR_PER for a list of element structs.");
            return false;
        }
    }
};

/// `&E::f` names an `ExprRef` member of a list element. Only meaningful inside a
/// `PerElementEdge`, where one such edge contributes one child per element. The
/// element reference is forwarded as-is, so the same machinery serves the const
/// walk (`const E &`) and the mutating walk (`E &`).
template <auto Member> struct ElementEdge {
    using class_type = typename member_pointer_traits<decltype(Member)>::class_type;
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, ExprRef>;
    }

    template <typename ElementT, typename Sink>
    [[nodiscard]] static bool walk(ElementT &element, Sink &sink) {
        static_assert(std::is_same_v<std::remove_cvref_t<ElementT>, class_type>,
                      "expr_nodes.def element edge names a member of a different element "
                      "type than the list member it is used with");
        auto &child = element.*Member;
        return !child || sink(*child);
    }
};

/// Ordered element-edge sequence applied to each list element.
template <typename... Edges> struct ElementEdgeList {
    [[nodiscard]] static constexpr bool valid() noexcept {
        return (Edges::valid() && ...);
    }

    template <typename ElementT, typename Sink>
    [[nodiscard]] static bool walk(ElementT &element, Sink &sink) {
        return (Edges::template walk<ElementT>(element, sink) && ...);
    }
};

/// `&N::m` names a `std::vector<E>` member: one child edge per element per
/// element edge, in list order and, within one element, in edge order. This is
/// the grouped form — for `match` arms it visits `arm.guard` then `arm.body`
/// before moving to the next arm, matching the hand-written per-arm loop this
/// derivation replaces (a flat per-member fan-out would visit all guards before
/// any body and silently reorder the traversal).
template <auto ListMember, typename ElementEdges> struct PerElementEdge {
    using list_type = typename member_pointer_traits<decltype(ListMember)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return ElementEdges::valid() && requires { typename vector_element<list_type>::type; };
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        if constexpr (requires { typename vector_element<list_type>::type; }) {
            for (auto &element : node.*ListMember) {
                if (!ElementEdges::template walk<decltype(element)>(element, sink)) {
                    return false;
                }
            }
            return true;
        } else {
            static_assert(always_false<NodeT>,
                          "AHFL_EXPR_PER names a member that is not a std::vector of element "
                          "structs; use AHFL_EXPR_FANOUT for a std::vector<ExprRef> member.");
            return false;
        }
    }
};

/// Ordered, short-circuiting conjunction of one node's child edges.
template <typename... Edges> struct EdgeList {
    [[nodiscard]] static constexpr bool valid() noexcept {
        return (Edges::valid() && ...);
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        return (Edges::template walk<NodeT>(node, sink) && ...);
    }
};

/// Child-edge set of one ExprNode alternative, specialized once per node by the
/// expansion of expr_nodes.def below. A node with no specialization is a build
/// error that names the type (see the static_assert at the bottom).
template <typename Node> struct ChildEdges {
    static_assert(always_false<Node>,
                  "expr_nodes.def: this node declares no child edges. Add its "
                  "HANDLE_EXPR_NODE entry with an edge list (`()` for a leaf) so the "
                  "derived const/mut traversal can reach its children.");
};

// Edge-column spellings consumed by expr_nodes.def. The third HANDLE_EXPR_NODE
// argument is always a parenthesized, comma-separated edge list; AHFL_EXPR_UNPAREN
// strips the one level of parentheses a template argument list cannot carry, and
// each kind names one edge. A leaf is spelled `()`.
#define AHFL_EXPR_UNPAREN(...) __VA_ARGS__
#define AHFL_EXPR_BRANCH(Member) BranchEdge<Member>
#define AHFL_EXPR_FANOUT(Member) FanoutEdge<Member>
#define AHFL_EXPR_PER(ListMember, ...)                                                  \
    PerElementEdge<ListMember,                                                          \
                   ElementEdgeList<AHFL_EXPR_UNPAREN(__VA_ARGS__)>>
#define AHFL_EXPR_ELEM(Member) ElementEdge<Member>

#define HANDLE_EXPR_NODE(Name, Wire, Edges)                                             \
    template <> struct ChildEdges<Name> {                                               \
        using type = EdgeList<AHFL_EXPR_UNPAREN Edges>;                                  \
    };                                                                                  \
    static_assert(ChildEdges<Name>::type::valid(),                                      \
                  "expr_nodes.def: node " #Name                                         \
                  " has child-edge metadata that does not match its fields");
#include "ahfl/compiler/ir/expr_nodes.def"

#undef AHFL_EXPR_ELEM
#undef AHFL_EXPR_PER
#undef AHFL_EXPR_FANOUT
#undef AHFL_EXPR_BRANCH
#undef AHFL_EXPR_UNPAREN

/// Walk the derived child edges of one node alternative (const or mutable),
/// invoking `sink` with each child `Expr`. `sink` returns `false` to stop the
/// walk (the visitor's `Abort`), short-circuiting the remaining edges.
template <typename NodeT, typename Sink>
[[nodiscard]] bool walk_children(NodeT &node, Sink &sink) {
    using Edges = typename ChildEdges<std::remove_cvref_t<NodeT>>::type;
    return Edges::template walk<NodeT>(node, sink);
}

/// Walk the derived child edges of a whole expression, for the const and the
/// mutating traversal alike.
template <typename Sink> [[nodiscard]] bool walk_children(const Expr &expr, Sink &sink) {
    return std::visit([&](const auto &node) -> bool { return walk_children(node, sink); },
                      expr.node);
}

template <typename Sink> [[nodiscard]] bool walk_children(Expr &expr, Sink &sink) {
    return std::visit([&](auto &node) -> bool { return walk_children(node, sink); }, expr.node);
}

// RFC 0027 P6/P7 (KR6.13-T) compile-time coverage gate: every alternative of the
// generated ExprNode variant MUST have received a ChildEdges specialization from
// expr_nodes.def. Without this pin a node added to the .def without an edge list
// would keep compiling until something walked it — the silent-skip class this
// slice removes.
template <typename Variant, std::size_t... Is>
[[nodiscard]] consteval bool all_alternatives_declare_edges(std::index_sequence<Is...>) {
    return (requires { typename ChildEdges<std::variant_alternative_t<Is, Variant>>::type; } &&
            ...);
}

static_assert(all_alternatives_declare_edges<ExprNode>(
                  std::make_index_sequence<std::variant_size_v<ExprNode>>{}),
              "expr_nodes.def is incomplete: an ahfl::ir::ExprNode alternative has no "
              "child-edge declaration. Give every node an entry with an edge list (`()` "
              "for a leaf).");

} // namespace ahfl::ir::expr_child_detail

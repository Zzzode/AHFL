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
// Exhaustiveness AND exact coverage are STRUCTURAL compile-time properties here,
// not conventions. Declaring an edge list at all is not enough — the metadata
// must name exactly the child-bearing members the struct actually has:
//   * a node added to `expr_nodes.def` without an edge entry has no
//     `ChildEdges<Name>` specialization, so the `requires`-fold pin at the
//     bottom of this header — and any `walk_children` instantiation for it,
//     which the derived `std::visit` in visitor.cpp reaches for every
//     alternative — is a hard error naming the type;
//   * `child_edges_cover` (below) counts the node struct's `ExprRef` members,
//     its `std::vector<ExprRef>` members and its vectors of element structs that
//     themselves carry an `ExprRef` — by aggregate-initialization probing, no
//     reflection extension — and the per-node `static_assert` emitted from that
//     same .def line requires the edge list to cover exactly them. So `()` on a
//     node whose struct HAS a child, or a child member added to an existing
//     struct without a matching edge, is a build error, not a silent skip;
//   * `EdgeList::valid()` rejects a malformed edge AND a member named twice
//     (which would double-visit / re-stamp that child), and `PerElementEdge`
//     rejects an element edge whose element class differs from the list member's
//     element type — the message names the offending member.
// tests/fixtures/ir/ssot/expr_child_edges_negative.cpp pins the uncovered-node,
// the leaf-wired-with-children and the duplicated-edge directions.

#include "ahfl/compiler/ir/expr.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir::expr_child_detail {

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

// ---------------------------------------------------------------------------
// Structural member probe (reflection-free).
//
// The child-edge list is DERIVED metadata, and the failure this header exists to
// prevent is a *silent* one: a node — or a list element — whose struct gained a
// child member that no edge names. Metadata alone cannot see that: the list
// agrees with itself. The gate below therefore pairs the declared edges with the
// STRUCT.
//
// The scan is aggregate-initialization probing. `Node{...}` with a wildcard in
// one position compiles iff that position's member accepts the wildcard, so a
// wildcard that accepts everything EXCEPT one member type identifies, per
// position, whether that member is of that type. `Any` accepts anything; `NoRef`
// rejects only `ExprRef`; `NoVecRef` rejects only `std::vector<ExprRef>`;
// `ChildVector` converts only to a `std::vector<E>` whose element struct `E`
// itself carries an `ExprRef`. No compiler reflection extension, no `<cstddef>`
// magic counts — just overload resolution on the real member list.
// ---------------------------------------------------------------------------
namespace probe {

/// Accepts any member type, `ExprRef` included.
struct Any {
    template <typename T> operator T() const;
};

/// Accepts any member type EXCEPT `ExprRef`: `Node{..., NoRef{}, ...}` fails to
/// compile exactly when the probed member is an `ExprRef`.
struct NoRef {
    template <typename T>
        requires(!std::is_same_v<T, ExprRef>)
    operator T() const;
};

/// Accepts any member type EXCEPT `std::vector<ExprRef>`.
struct NoVecRef {
    template <typename T>
        requires(!std::is_same_v<T, std::vector<ExprRef>>)
    operator T() const;
};

template <typename Node> [[nodiscard]] consteval std::size_t expr_ref_member_count();

/// Accepts a `std::vector<E>` only when `E` is an aggregate carrying at least
/// one `ExprRef` — i.e. exactly the list-of-element-structs shape that
/// `AHFL_EXPR_PER` consumes. Not convertible to `std::vector<ExprRef>` itself
/// (an `ExprRef` is not an aggregate), so it never double-counts a FANOUT.
struct ChildVector {
    template <typename E>
        requires(std::is_aggregate_v<E> && expr_ref_member_count<E>() > 0)
    operator std::vector<E>() const;
};

// Does `Node{args...}`, every argument the same `Filler` type, compile for N args?
template <typename Node, typename Filler, typename Seq> struct inits_all_impl;
template <typename Node, typename Filler, std::size_t... Is>
struct inits_all_impl<Node, Filler, std::index_sequence<Is...>> {
    static constexpr bool value = requires { Node{((void)Is, Filler{})...}; };
};
template <typename Node, typename Filler, std::size_t N>
inline constexpr bool inits_all = inits_all_impl<Node, Filler, std::make_index_sequence<N>>::value;

/// Number of public data members of the simple aggregate `Node` (0 when it is
/// not an aggregate, in which case the probe cannot speak for it).
template <typename Node, std::size_t N = 0> [[nodiscard]] consteval std::size_t member_count() {
    if constexpr (!std::is_aggregate_v<Node>) {
        return 0;
    } else if constexpr (N >= 16) {
        return 0; // defensive stop; every AHFL-IR node is far below this
    } else if constexpr (inits_all<Node, Any, N + 1>) {
        return member_count<Node, N + 1>();
    } else {
        return N;
    }
}

// `Node{ Any..., Filler{}, Any... }` with Filler in position K, the rest `Any`.
template <typename Node, typename Filler, std::size_t K, std::size_t N, typename Pre, typename Post>
struct inits_at_impl;
template <typename Node,
          typename Filler,
          std::size_t K,
          std::size_t N,
          std::size_t... Pre,
          std::size_t... Post>
struct inits_at_impl<Node,
                     Filler,
                     K,
                     N,
                     std::index_sequence<Pre...>,
                     std::index_sequence<Post...>> {
    static constexpr bool value =
        requires { Node{((void)Pre, Any{})..., Filler{}, ((void)Post, Any{})...}; };
};
template <typename Node, typename Filler, std::size_t K, std::size_t N>
inline constexpr bool inits_at = inits_at_impl<Node,
                                               Filler,
                                               K,
                                               N,
                                               std::make_index_sequence<K>,
                                               std::make_index_sequence<N - K - 1>>::value;

/// Count the positions of `Node` that do / do not accept a `Filler` argument.
template <typename Node, typename Filler, bool Accept>
[[nodiscard]] consteval std::size_t count_positions() {
    if constexpr (!std::is_aggregate_v<Node>) {
        return 0;
    } else {
        constexpr std::size_t n = member_count<Node>();
        std::size_t count = 0;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((count += (inits_at<Node, Filler, Is, n> == Accept ? std::size_t{1} : std::size_t{0})),
             ...);
        }(std::make_index_sequence<n>{});
        return count;
    }
}

template <typename Node> [[nodiscard]] consteval std::size_t expr_ref_member_count() {
    return count_positions<Node, NoRef, false>();
}

template <typename Node> [[nodiscard]] consteval std::size_t expr_ref_vector_member_count() {
    return count_positions<Node, NoVecRef, false>();
}

/// Vectors of element structs that themselves carry a child (`AHFL_EXPR_PER`).
template <typename Node> [[nodiscard]] consteval std::size_t child_vector_member_count() {
    return count_positions<Node, ChildVector, true>();
}

} // namespace probe

/// Which structural member class an edge consumes. The coverage gate compares
/// the per-kind counts of a node's declared edges against the struct's per-kind
/// member counts.
enum class EdgeKind : std::uint8_t {
    Branch,
    Fanout,
    PerElement,
    Element
};

/// True when no two of `Edges...` name the same member: a member listed twice
/// would be visited (and, on the mutating walk, re-stamped) twice.
template <std::size_t I, std::size_t J, typename Tuple>
[[nodiscard]] consteval bool members_differ() noexcept {
    if constexpr (I >= J) {
        return true;
    } else if constexpr (requires {
                             std::tuple_element_t<I, Tuple>::member;
                             std::tuple_element_t<J, Tuple>::member;
                         } && std::is_same_v<decltype(std::tuple_element_t<I, Tuple>::member),
                                             decltype(std::tuple_element_t<J, Tuple>::member)>) {
        return std::tuple_element_t<I, Tuple>::member != std::tuple_element_t<J, Tuple>::member;
    } else {
        // Different member-pointer types can never designate the same member.
        return true;
    }
}

template <typename Tuple, std::size_t I, std::size_t... Js>
[[nodiscard]] consteval bool members_distinct_row(std::index_sequence<Js...>) noexcept {
    return (members_differ<I, Js, Tuple>() && ...);
}

template <typename Tuple, std::size_t... Is>
[[nodiscard]] consteval bool members_distinct(std::index_sequence<Is...>) noexcept {
    return (members_distinct_row<Tuple, Is>(std::make_index_sequence<sizeof...(Is)>{}) && ...);
}

template <typename... Edges> [[nodiscard]] consteval bool all_members_distinct() noexcept {
    return members_distinct<std::tuple<Edges...>>(std::make_index_sequence<sizeof...(Edges)>{});
}

/// `&N::m` names an `ExprRef` member: one child edge.
template <auto Member> struct BranchEdge {
    static constexpr EdgeKind kind = EdgeKind::Branch;
    static constexpr auto member = Member;
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, ExprRef>;
    }

    [[nodiscard]] static consteval bool covers_element() noexcept {
        return true;
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(node.*Member)>, ExprRef>,
                      "AHFL_EXPR_BRANCH names a member that is not an ExprRef; use "
                      "AHFL_EXPR_FANOUT for a std::vector<ExprRef> or AHFL_EXPR_PER "
                      "for a list of element structs.");
        auto &field = node.*Member;
        return !field || sink(*field);
    }
};

/// `&N::m` names a `std::vector<ExprRef>` member: one child edge per element.
template <auto Member> struct FanoutEdge {
    static constexpr EdgeKind kind = EdgeKind::Fanout;
    static constexpr auto member = Member;
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, std::vector<ExprRef>>;
    }

    [[nodiscard]] static consteval bool covers_element() noexcept {
        return true;
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        static_assert(
            std::is_same_v<std::remove_cvref_t<decltype(node.*Member)>, std::vector<ExprRef>>,
            "AHFL_EXPR_FANOUT names a member that is not a std::vector<ExprRef>; use "
            "AHFL_EXPR_BRANCH for one ExprRef or AHFL_EXPR_PER for a list of element "
            "structs.");
        for (auto &child : node.*Member) {
            if (child && !sink(*child)) {
                return false;
            }
        }
        return true;
    }
};

/// `&E::f` names an `ExprRef` member of a list element. Only meaningful inside a
/// `PerElementEdge`, where one such edge contributes one child per element. The
/// element reference is forwarded as-is, so the same machinery serves the const
/// walk (`const E &`) and the mutating walk (`E &`).
template <auto Member> struct ElementEdge {
    static constexpr EdgeKind kind = EdgeKind::Element;
    static constexpr auto member = Member;
    using class_type = typename member_pointer_traits<decltype(Member)>::class_type;
    using value_type = typename member_pointer_traits<decltype(Member)>::value_type;

    [[nodiscard]] static constexpr bool valid() noexcept {
        return std::is_same_v<value_type, ExprRef>;
    }

    [[nodiscard]] static consteval bool covers_element() noexcept {
        return true;
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
    static_assert(sizeof...(Edges) > 0,
                  "AHFL_EXPR_PER needs at least one AHFL_EXPR_ELEM edge; spell a childless node "
                  "`()` and a plain std::vector<ExprRef> member AHFL_EXPR_FANOUT.");
    static_assert((Edges::valid() && ...) && all_members_distinct<Edges...>(),
                  "expr_nodes.def: an AHFL_EXPR_PER element edge names a non-ExprRef member, or "
                  "names the same element member twice");

    [[nodiscard]] static constexpr bool valid() noexcept {
        return (Edges::valid() && ...) && all_members_distinct<Edges...>();
    }

    template <EdgeKind Kind> [[nodiscard]] static consteval std::size_t count_of() noexcept {
        return ((Edges::kind == Kind ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
    }

    /// Every element edge names a member of `ElementT` (the list member's element
    /// type). `IAHFL_EXPR_PER` mixing an element edge from another element struct
    /// in is the mistake this rejects.
    template <typename ElementT> [[nodiscard]] static constexpr bool matches_class() noexcept {
        return (std::is_same_v<ElementT, typename Edges::class_type> && ...);
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
    static constexpr EdgeKind kind = EdgeKind::PerElement;
    static constexpr auto member = ListMember;
    using list_type = typename member_pointer_traits<decltype(ListMember)>::value_type;

    /// The list member's element type, or `void` when it is not a `std::vector`.
    [[nodiscard]] static consteval bool list_is_vector() noexcept {
        return requires { typename vector_element<list_type>::type; };
    }

    /// The names of the element edges all belong to the list member's element
    /// type. Only meaningful when `list_is_vector()`; `true` otherwise so the
    /// (more specific) "not a vector" diagnostic is the one that surfaces.
    [[nodiscard]] static consteval bool element_class_matches() noexcept {
        if constexpr (requires { typename vector_element<list_type>::type; }) {
            return ElementEdges::template matches_class<typename vector_element<list_type>::type>();
        } else {
            return true;
        }
    }

    [[nodiscard]] static constexpr bool valid() noexcept {
        return ElementEdges::valid() && list_is_vector() && element_class_matches();
    }

    /// The element edge list covers exactly the element struct's child members.
    [[nodiscard]] static consteval bool covers_element() noexcept {
        if constexpr (requires { typename vector_element<list_type>::type; }) {
            return ElementEdges::template count_of<EdgeKind::Element>() ==
                   probe::expr_ref_member_count<typename vector_element<list_type>::type>();
        } else {
            return true; // valid() already rejects the non-vector member
        }
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        static_assert(list_is_vector(),
                      "AHFL_EXPR_PER names a member that is not a std::vector of element structs; "
                      "use AHFL_EXPR_FANOUT for a std::vector<ExprRef> member or "
                      "AHFL_EXPR_BRANCH for one ExprRef.");
        static_assert(element_class_matches(),
                      "AHFL_EXPR_PER element edge names a member of a different element type than "
                      "the list member's elements");
        if constexpr (requires { typename vector_element<list_type>::type; }) {
            for (auto &element : node.*ListMember) {
                if (!ElementEdges::template walk<decltype(element)>(element, sink)) {
                    return false;
                }
            }
            return true;
        } else {
            return false; // unreachable: the static_assert above already failed
        }
    }
};

/// Ordered, short-circuiting conjunction of one node's child edges.
template <typename... Edges> struct EdgeList {
    [[nodiscard]] static constexpr bool valid() noexcept {
        return (Edges::valid() && ...) && all_members_distinct<Edges...>();
    }

    template <EdgeKind Kind> [[nodiscard]] static consteval std::size_t count_of() noexcept {
        return ((Edges::kind == Kind ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
    }

    [[nodiscard]] static consteval bool all_elements_covered() noexcept {
        return (Edges::covers_element() && ...);
    }

    template <typename NodeT, typename Sink>
    [[nodiscard]] static bool walk(NodeT &node, Sink &sink) {
        return (Edges::template walk<NodeT>(node, sink) && ...);
    }
};

/// Per-node structural coverage gate: the declared edges must name EXACTLY the
/// node struct's child-bearing members — no member left unnamed (the silent-skip
/// data-loss class) and no edge of a kind the struct cannot supply. Emitted by
/// the same `HANDLE_EXPR_NODE` line that declares the node, so the diagnostic
/// names the node.
template <typename NodeT, typename Edges>
[[nodiscard]] consteval bool child_edges_cover() noexcept {
    if constexpr (!std::is_aggregate_v<NodeT>) {
        return true; // not probed; the primary-template pin covers the node set
    } else {
        return Edges::template count_of<EdgeKind::Branch>() ==
                   probe::expr_ref_member_count<NodeT>() &&
               Edges::template count_of<EdgeKind::Fanout>() ==
                   probe::expr_ref_vector_member_count<NodeT>() &&
               Edges::template count_of<EdgeKind::PerElement>() ==
                   probe::child_vector_member_count<NodeT>() &&
               Edges::all_elements_covered();
    }
}

/// Child-edge set of one ExprNode alternative, specialized once per node by the
/// expansion of expr_nodes.def below. The primary template is deliberately LEFT
/// INCOMPLETE: naming `ChildEdges<N>::type` for an unspecialized `N` is a
/// substitution failure, so the `requires`-fold pin at the bottom of this header
/// can *evaluate* it and report the uncovered node by name. (An `always_false`
/// body here instead made that `requires` a hard error the instant it was
/// formed, so the pin could never be the assert that fired.)
template <typename Node> struct ChildEdges;

// Edge-column spellings consumed by expr_nodes.def. The third HANDLE_EXPR_NODE
// argument is always a parenthesized, comma-separated edge list; AHFL_EXPR_UNPAREN
// strips the one level of parentheses a template argument list cannot carry, and
// each kind names one edge. A leaf is spelled `()`.
#define AHFL_EXPR_UNPAREN(...) __VA_ARGS__
#define AHFL_EXPR_BRANCH(Member) BranchEdge<Member>
#define AHFL_EXPR_FANOUT(Member) FanoutEdge<Member>
#define AHFL_EXPR_PER(ListMember, ...)                                                             \
    PerElementEdge<ListMember, ElementEdgeList<AHFL_EXPR_UNPAREN(__VA_ARGS__)>>
#define AHFL_EXPR_ELEM(Member) ElementEdge<Member>

#define HANDLE_EXPR_NODE(Name, Wire, Edges)                                                        \
    template <> struct ChildEdges<Name> {                                                          \
        using type = EdgeList<AHFL_EXPR_UNPAREN Edges>;                                            \
    };                                                                                             \
    static_assert(ChildEdges<Name>::type::valid(),                                                 \
                  "expr_nodes.def: node " #Name                                                    \
                  " has child-edge metadata that does not match its fields (a malformed "          \
                  "edge, or the same member named twice)");                                        \
    static_assert(child_edges_cover<Name, typename ChildEdges<Name>::type>(),                      \
                  "expr_nodes.def: node " #Name                                                    \
                  " child-edge list does not cover its struct's child-bearing members "            \
                  "(an ExprRef, a std::vector<ExprRef>, or a vector of element structs "           \
                  "carrying an ExprRef is missing an edge; or an edge names a member "             \
                  "the struct does not have");
#include "ahfl/compiler/ir/expr_nodes.def"
#undef AHFL_EXPR_ELEM
#undef AHFL_EXPR_PER
#undef AHFL_EXPR_FANOUT
#undef AHFL_EXPR_BRANCH
#undef AHFL_EXPR_UNPAREN

/// Walk the derived child edges of one node alternative (const or mutable),
/// invoking `sink` with each child `Expr`. `sink` returns `false` to stop the
/// walk (the visitor's `Abort`), short-circuiting the remaining edges.
template <typename NodeT, typename Sink> [[nodiscard]] bool walk_children(NodeT &node, Sink &sink) {
    using Node = std::remove_cvref_t<NodeT>;
    static_assert(
        requires { typename ChildEdges<Node>::type; },
        "expr_nodes.def: this ExprNode alternative has no child-edge declaration. Add "
        "its HANDLE_EXPR_NODE entry with an edge list (`()` for a leaf) so the derived "
        "const/mut traversal can reach its children.");
    using Edges = typename ChildEdges<Node>::type;
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
// expr_nodes.def. This is the primary-template-independent pin — `ChildEdges` has
// an incomplete primary template (see above), so `requires { ...::type; }` is a
// substitution failure for an uncovered alternative and this `requires`-fold
// evaluates to `false` and fires with the coverage message, rather than the
// child node's `type` name failing inside some unrelated instantiation.
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

#pragma once

// RFC 0027 P6/P7/P8 (KR6.13-F): shared tag-tuple -> std::variant reconstruction.
//
// Every IR node variant whose alternative list lives in an X-macro `.def` file
// is generated through this indirection. A C++ template ARGUMENT list (unlike a
// braced-init list) rejects a trailing comma, so an X-macro cannot paste
// `Name,` straight into `std::variant<...>`; instead each `.def` line emits one
// `node_tag<Name>` value into a `std::tuple`, and the variant is reconstructed
// from that tuple's element types. `expr_nodes.def` / `core_value_types.def`
// grew this helper locally first; KR6.13-F hoists it here so the statement /
// temporal / pattern / declaration / Core families share ONE definition
// (CLAUDE.md: no parallel SSOT copies).

#include <tuple>
#include <type_traits>
#include <variant>

namespace ahfl::ir::node_detail {

template <typename T> struct node_tag {
    using type = T;
};

template <typename TagTuple> struct variant_from_tags;

template <typename... Ts> struct variant_from_tags<std::tuple<node_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

/// `std::variant<...>` reconstructed, in declaration order, from a tag tuple.
template <typename TagTuple>
using variant_from_tags_t = typename variant_from_tags<std::remove_cvref_t<TagTuple>>::type;

} // namespace ahfl::ir::node_detail

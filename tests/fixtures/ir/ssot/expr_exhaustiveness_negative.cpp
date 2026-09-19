// Negative compile-test fixture for the RFC 0027 P6/P7 (KR6.13-E) ExprNode
// X-macro exhaustiveness gate, the production-list counterpart of
// core_value_type_nodes_negative.cpp. This TU is syntax-only-compiled twice by
// tests/scripts/ir_ssot_compile_fail.py:
//
//   * clean build (no macro): the variant rebuilt from the PRODUCTION node list
//     include/ahfl/compiler/ir/expr_nodes.def is static_assert-identical to
//     ahfl::ir::ExprNode, and the one-handler-per-alternative visitor (also
//     generated from the .def) covers every alternative, so compilation MUST
//     succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED): one dummy extra
//     alternative (SsotUnhandledExpr) is appended to the rebuilt variant WITHOUT
//     a matching visitor handler, so compilation MUST fail with a diagnostic
//     that names SsotUnhandledExpr.
//
// Like the Core fixture (and unlike the pre-KR6.13-E Expr fixture), this one
// consumes the SAME expr_nodes.def the compiler generates the variant and wire
// table from, so a node added to the production list without a visitor handler
// is caught without maintaining a second copy of the node list.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir::ssot_exhaustiveness_test {

// Sentinel "new node" the exhaustive visitor deliberately does not handle.
// Its name MUST surface in the compiler diagnostic; the Python harness greps
// for it.
struct SsotUnhandledExpr {};

} // namespace ahfl::ir::ssot_exhaustiveness_test

namespace {

template <typename T> struct type_tag {
    using type = T;
};

#define HANDLE_EXPR_NODE(Name, Wire, Edges) type_tag<ahfl::ir::Name>{},

// Trailing commas are legal in braced-init lists (unlike template argument
// lists), which lets the X-macro emit comma-suffixed entries uniformly.
constexpr auto kExprNodeTags = std::tuple{
#include "ahfl/compiler/ir/expr_nodes.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED
    type_tag<ahfl::ir::ssot_exhaustiveness_test::SsotUnhandledExpr>{}
#endif
};

template <typename Tuple> struct tags_to_variant;
template <typename... Ts> struct tags_to_variant<std::tuple<type_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureExprNode =
    tags_to_variant<std::remove_cvref_t<decltype(kExprNodeTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED
// The production .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureExprNode, ahfl::ir::ExprNode>,
              "include/ahfl/compiler/ir/expr_nodes.def drifted from "
              "ahfl::ir::ExprNode; the X-list and the generated variant must "
              "stay in lockstep.");
#endif

template <typename... Handlers> struct overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_visit(FixtureExprNode &node) {
#define HANDLE_EXPR_NODE(Name, Wire, Edges) [](const ahfl::ir::Name &) {},
    overloaded visitor{
#include "ahfl/compiler/ir/expr_nodes.def"
    };
    std::visit(visitor, node);
}

} // namespace

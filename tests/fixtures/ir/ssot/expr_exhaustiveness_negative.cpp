// Negative compile-test fixture for the RFC 0027 P8 IR SSOT exhaustiveness
// gate (KR6.13-G). This TU is syntax-only-compiled twice by
// tests/scripts/ir_ssot_compile_fail.py:
//
//   * clean build (no macro): the rebuilt variant is identical to
//     ahfl::ir::ExprNode and the one-handler-per-alternative visitor covers
//     every alternative, so compilation MUST succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED): one dummy extra
//     alternative (SsotUnhandledExpr) is appended to the variant type list
//     WITHOUT a matching visitor handler, so compilation MUST fail with a
//     diagnostic that names SsotUnhandledExpr.
//
// The alternative type list is the shared X-macro in expr_nodes.def; the
// standalone compile resolves it relative to this file's directory.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/expr.hpp"

#include "expr_nodes.def"

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

#define AHFL_SSOT_EXPR_TAG(Name) type_tag<ahfl::ir::Name>{},

// Trailing commas are legal in braced-init lists (unlike template argument
// lists), which lets the X-macro emit comma-suffixed entries uniformly.
constexpr auto kExprNodeTags = std::tuple{
    AHFL_IR_EXPR_NODES(AHFL_SSOT_EXPR_TAG)
#ifdef AHFL_SSOT_INJECT_UNHANDLED
    type_tag<ahfl::ir::ssot_exhaustiveness_test::SsotUnhandledExpr>{}
#endif
};

#undef AHFL_SSOT_EXPR_TAG

template <typename Tuple> struct tags_to_variant;
template <typename... Ts> struct tags_to_variant<std::tuple<type_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureExprNode =
    tags_to_variant<std::remove_cvref_t<decltype(kExprNodeTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED
// The shared .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureExprNode, ahfl::ir::ExprNode>,
              "tests/fixtures/ir/ssot/expr_nodes.def drifted from "
              "ahfl::ir::ExprNode; keep the SSOT list and the production "
              "variant in lockstep.");
#endif

template <typename... Handlers> struct overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_visit(FixtureExprNode &node) {
    overloaded visitor{
#define AHFL_SSOT_EXPR_HANDLER(Name) [](const ahfl::ir::Name &) {},
        AHFL_IR_EXPR_NODES(AHFL_SSOT_EXPR_HANDLER)
#undef AHFL_SSOT_EXPR_HANDLER
    };
    std::visit(visitor, node);
}

} // namespace

// Negative compile-test fixture for the RFC 0027 P6/P7/P8 (KR6.13-F)
// Decl X-macro exhaustiveness gate. Member of the KR6.13-F family
// (ExprNode / StatementNode / TemporalExprNode / MatchPatternNode / Decl / Core
// value types). It consumes the PRODUCTION node list
// include/ahfl/compiler/ir/decl_nodes.def so a node added there without a visitor handler is caught
// without maintaining a second copy of the node list.
//
// tests/scripts/ir_ssot_compile_fail.py syntax-only-compiles this TU twice:
//
//   * clean build (no macro): the variant rebuilt from the production node list
//     is static_assert-identical to ahfl::ir::Decl, and the
//     one-handler-per-node visitor (also generated from the .def) covers all
//     16 alternatives, so compilation MUST succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED_DECL): one dummy extra alternative
//     (SsotUnhandledDecl) is appended to the rebuilt variant
//     WITHOUT a matching visitor handler, so compilation MUST fail with a
//     diagnostic that names SsotUnhandledDecl.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/program.hpp"

namespace ahfl::ir::ssot_decl_test {

// Sentinel "new node" the exhaustive visitor deliberately does not handle. Its
// name MUST surface in the compiler diagnostic; the Python harness greps for it.
struct SsotUnhandledDecl {};

} // namespace ahfl::ir::ssot_decl_test

namespace {

template <typename T> struct type_tag {
    using type = T;
};

#define HANDLE_DECL_NODE(Name) type_tag<ahfl::ir::Name>{},

constexpr auto kFixtureTags = std::tuple{
#include "ahfl/compiler/ir/decl_nodes.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED_DECL
    type_tag<ahfl::ir::ssot_decl_test::SsotUnhandledDecl>{}
#endif
};

template <typename Tuple> struct tags_to_variant;
template <typename... Ts> struct tags_to_variant<std::tuple<type_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureNode = tags_to_variant<std::remove_cvref_t<decltype(kFixtureTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED_DECL
// The production .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureNode, ahfl::ir::Decl>,
              "include/ahfl/compiler/ir/decl_nodes.def drifted from "
              "ahfl::ir::Decl; the X-list and the generated variant "
              "must stay in lockstep.");
#endif

template <typename... Handlers> struct fixture_overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_visit(FixtureNode &node) {
#define HANDLE_DECL_NODE(Name) [](const ahfl::ir::Name &) {},
    fixture_overloaded visitor{
#include "ahfl/compiler/ir/decl_nodes.def"
    };
    std::visit(visitor, node);
}

} // namespace

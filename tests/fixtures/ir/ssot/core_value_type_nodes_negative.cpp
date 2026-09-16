// Negative compile-test fixture for the RFC 0027 Q1 CoreValueTypeNode X-macro
// exhaustiveness gate (KR6.13-X), the Core counterpart of
// expr_exhaustiveness_negative.cpp. This TU is syntax-only-compiled twice by
// tests/scripts/ir_ssot_compile_fail.py:
//
//   * clean build (no macro): the variant rebuilt from the PRODUCTION node list
//     include/ahfl/compiler/ir/core_value_types.def is static_assert-identical
//     to ahfl::ir::core::CoreValueTypeNode, and the one-handler-per-node visitor
//     (also generated from the .def) covers all 14 alternatives, so compilation
//     MUST succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED_VT): one dummy 15th
//     alternative (SsotUnhandledVt) is appended to the rebuilt variant WITHOUT a
//     matching visitor handler, so compilation MUST fail with a diagnostic that
//     names SsotUnhandledVt.
//
// Unlike the Expr fixture, this one consumes the SAME core_value_types.def the
// compiler uses, so a node added to the production list without a visitor
// handler is caught without maintaining a second copy of the node list.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core::ssot_core_vt_test {

// Sentinel "new node" the exhaustive visitor deliberately does not handle. Its
// name MUST surface in the compiler diagnostic; the Python harness greps for it.
struct SsotUnhandledVt {};

} // namespace ahfl::ir::core::ssot_core_vt_test

namespace {

template <typename T> struct core_vt_tag {
    using type = T;
};

#define HANDLE_CORE_VT(Name) core_vt_tag<ahfl::ir::core::CoreVt##Name>{},

// Trailing commas are legal in braced-init lists (unlike template argument
// lists), which lets the X-macro emit comma-suffixed entries uniformly.
constexpr auto kCoreVtTags = std::tuple{
#include "ahfl/compiler/ir/core_value_types.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED_VT
    core_vt_tag<ahfl::ir::core::ssot_core_vt_test::SsotUnhandledVt>{}
#endif
};

template <typename Tuple> struct core_vt_variant_of;
template <typename... Ts>
struct core_vt_variant_of<std::tuple<core_vt_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureCoreValueTypeNode =
    core_vt_variant_of<std::remove_cvref_t<decltype(kCoreVtTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED_VT
// The production .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureCoreValueTypeNode,
                             ahfl::ir::core::CoreValueTypeNode>,
              "include/ahfl/compiler/ir/core_value_types.def drifted from "
              "ahfl::ir::core::CoreValueTypeNode; the X-list and the generated "
              "variant must stay in lockstep.");
#endif

template <typename... Handlers> struct core_vt_overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_core_vt_visit(FixtureCoreValueTypeNode &node) {
#define HANDLE_CORE_VT(Name) [](const ahfl::ir::core::CoreVt##Name &) {},
    core_vt_overloaded visitor{
#include "ahfl/compiler/ir/core_value_types.def"
    };
    std::visit(visitor, node);
}

} // namespace

// Negative compile-test fixture for the RFC 0027 P6/P7/P8 (KR6.13-P7)
// CoreStmtNode X-macro exhaustiveness gate — the Core counterpart of
// stmt_nodes_negative.cpp. This TU is syntax-only-compiled twice by
// tests/scripts/ir_ssot_compile_fail.py:
//
//   * clean build (no macro): the variant rebuilt from the PRODUCTION node list
//     include/ahfl/compiler/ir/core_stmt_nodes.def is static_assert-identical to
//     ahfl::ir::core::CoreStmtNode, and the one-handler-per-node visitor (also
//     generated from the .def) covers all 9 alternatives, so compilation MUST
//     succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED_CORE_STMT): one dummy 10th
//     alternative (SsotUnhandledCoreStmt) is appended to the rebuilt variant
//     WITHOUT a matching visitor handler, so compilation MUST fail with a
//     diagnostic that names SsotUnhandledCoreStmt.
//
// This fixture consumes the SAME core_stmt_nodes.def the compiler uses, so a
// node added to the production list without a visitor handler is caught without
// maintaining a second copy of the node list.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core::ssot_core_stmt_test {

// Sentinel "new node" the exhaustive visitor deliberately does not handle. Its
// name MUST surface in the compiler diagnostic; the Python harness greps for it.
struct SsotUnhandledCoreStmt {};

} // namespace ahfl::ir::core::ssot_core_stmt_test

namespace {

template <typename T> struct core_stmt_tag {
    using type = T;
};

#define HANDLE_CORE_STMT_NODE(Name) core_stmt_tag<ahfl::ir::core::Name>{},

constexpr auto kCoreStmtTags = std::tuple{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED_CORE_STMT
    core_stmt_tag<ahfl::ir::core::ssot_core_stmt_test::SsotUnhandledCoreStmt>{}
#endif
};

template <typename Tuple> struct core_stmt_variant_of;
template <typename... Ts>
struct core_stmt_variant_of<std::tuple<core_stmt_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureCoreStmtNode = core_stmt_variant_of<std::remove_cvref_t<decltype(kCoreStmtTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED_CORE_STMT
// The production .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureCoreStmtNode, ahfl::ir::core::CoreStmtNode>,
              "include/ahfl/compiler/ir/core_stmt_nodes.def drifted from "
              "ahfl::ir::core::CoreStmtNode; the X-list and the generated "
              "variant must stay in lockstep.");
#endif

template <typename... Handlers> struct core_stmt_overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_core_stmt_visit(FixtureCoreStmtNode &node) {
#define HANDLE_CORE_STMT_NODE(Name) [](const ahfl::ir::core::Name &) {},
    core_stmt_overloaded visitor{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
    };
    std::visit(visitor, node);
}

} // namespace

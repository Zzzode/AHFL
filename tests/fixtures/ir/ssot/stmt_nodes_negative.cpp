// Negative compile-test fixture for the RFC 0027 P6/P7/P8 (KR6.13-F)
// StatementNode X-macro exhaustiveness gate. Second member of the KR6.13-F
// family (ExprNode already had one at expr_exhaustiveness_negative.cpp): where
// that pinned the expr variant, this pins the statement variant. Both consume
// the PRODUCTION node list (include/ahfl/compiler/ir/stmt_nodes.def), so a node
// added there without a visitor handler is caught without maintaining a second
// copy of the node list.
//
// tests/scripts/ir_ssot_compile_fail.py syntax-only-compiles this TU twice:
//
//   * clean build (no macro): the variant rebuilt from the production node list
//     is static_assert-identical to ahfl::ir::StatementNode, and the
//     one-handler-per-node visitor (also generated from the .def) covers all 11
//     alternatives, so compilation MUST succeed.
//   * injected build (-DAHFL_SSOT_INJECT_UNHANDLED_STMT): one dummy 12th
//     alternative (SsotUnhandledStmt) is appended to the rebuilt variant WITHOUT
//     a matching visitor handler, so compilation MUST fail with a diagnostic
//     that names SsotUnhandledStmt.

#include <tuple>
#include <type_traits>
#include <variant>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir::ssot_stmt_test {

// Sentinel "new node" the exhaustive visitor deliberately does not handle. Its
// name MUST surface in the compiler diagnostic; the Python harness greps for it.
struct SsotUnhandledStmt {};

} // namespace ahfl::ir::ssot_stmt_test

namespace {

template <typename T> struct type_tag {
    using type = T;
};

#define HANDLE_STMT_NODE(Name, Wire) type_tag<ahfl::ir::Name>{},

constexpr auto kStmtTags = std::tuple{
#include "ahfl/compiler/ir/stmt_nodes.def"
#ifdef AHFL_SSOT_INJECT_UNHANDLED_STMT
    type_tag<ahfl::ir::ssot_stmt_test::SsotUnhandledStmt>{}
#endif
};

template <typename Tuple> struct tags_to_variant;
template <typename... Ts> struct tags_to_variant<std::tuple<type_tag<Ts>...>> {
    using type = std::variant<Ts...>;
};

using FixtureStatementNode = tags_to_variant<std::remove_cvref_t<decltype(kStmtTags)>>::type;

#ifndef AHFL_SSOT_INJECT_UNHANDLED_STMT
// The production .def list MUST stay an exact mirror of the production variant.
static_assert(std::is_same_v<FixtureStatementNode, ahfl::ir::StatementNode>,
              "include/ahfl/compiler/ir/stmt_nodes.def drifted from "
              "ahfl::ir::StatementNode; the X-list and the generated variant "
              "must stay in lockstep.");
#endif

template <typename... Handlers> struct stmt_overloaded : Handlers... {
    using Handlers::operator()...;
};

// A non-template function body forces std::visit to instantiate the
// exhaustiveness check at compile time.
[[maybe_unused]] void force_exhaustive_stmt_visit(FixtureStatementNode &node) {
#define HANDLE_STMT_NODE(Name, Wire) [](const ahfl::ir::Name &) {},
    stmt_overloaded visitor{
#include "ahfl/compiler/ir/stmt_nodes.def"
    };
    std::visit(visitor, node);
}

} // namespace

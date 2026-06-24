// Standalone structural checks for DecreasesClauseSyntax.
//
// Acceptance criteria driven:
//   * NOT a subclass of Decl / Node (R-09: not dispatchable via DeclKind)
//   * Plain aggregate with expected fields
//   * Header self-sufficient (include-what-you-use smoke)

#include "ahfl/compiler/frontend/ast.hpp"

#include <cstdio>
#include <type_traits>
#include <vector>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

void test_not_a_decl() {
    // R-09: the clause is a syntax fragment, not a declaration.  It must not
    // be dispatchable through Node / Decl inheritance.  Any base-class
    // relationship would leak it into DeclKind / visitor dispatch paths and
    // allow accidentally treating it as a top-level construct.
    using namespace ahfl::ast;
    check((!std::is_base_of<Node, DecreasesClauseSyntax>::value),
          "DecreasesClauseSyntax is NOT derived from Node");
    check((!std::is_base_of<Decl, DecreasesClauseSyntax>::value),
          "DecreasesClauseSyntax is NOT derived from Decl");
    check((std::is_standard_layout<DecreasesClauseSyntax>::value),
          "DecreasesClauseSyntax has standard layout (plain struct)");
}

void test_defaults() {
    using namespace ahfl::ast;
    DecreasesClauseSyntax clause;
    check(clause.terms.empty(), "default clause has empty terms vector");
    check(clause.is_wildcard == false, "default clause is not the wildcard form");
}

void test_field_access() {
    using namespace ahfl::ast;
    DecreasesClauseSyntax clause;
    clause.is_wildcard = true;

    // Wildcard form: both fields are independent.  A consumer iterating the
    // vector unconditionally must still observe empty() without crashing.
    check(clause.is_wildcard, "is_wildcard is observable");
    check(clause.terms.empty(), "terms still reachable after is_wildcard flip");

    // Explicit-term form: move Owned<ExprSyntax> into the vector and observe
    // size without moving again.
    auto expr = ahfl::make_owned<ExprSyntax>();
    expr->node = BoolLiteralExpr{true};
    expr->text = "true";
    clause.is_wildcard = false;
    clause.terms.push_back(std::move(expr));
    check(clause.terms.size() == 1, "terms vector accepts one Owned<ExprSyntax>");
    check(clause.terms[0] != nullptr, "stored term is non-null");
}

void test_copy_move_noexcept() {
    using namespace ahfl::ast;
    // vector<unique_ptr> move is noexcept; copy should be deleted / nothrow
    // as the default (empty / trivial for bool).  We only assert move is
    // noexcept because that's what container insertions rely on.
    check(std::is_nothrow_move_constructible<DecreasesClauseSyntax>::value,
          "DecreasesClauseSyntax is nothrow move constructible");
    check(std::is_nothrow_move_assignable<DecreasesClauseSyntax>::value,
          "DecreasesClauseSyntax is nothrow move assignable");
}

} // namespace

int main() {
    test_not_a_decl();
    test_defaults();
    test_field_access();
    test_copy_move_noexcept();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

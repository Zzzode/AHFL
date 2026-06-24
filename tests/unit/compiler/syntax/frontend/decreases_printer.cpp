// Standalone printer tests for DecreasesClauseSyntax (R-09: no DeclKind dispatch).
//
// Uses exact-string / substring assertions so we pin the shape callers can
// rely on for round-trip verification against formatter output.

#include "ahfl/compiler/frontend/frontend.hpp"

#include <cstdio>
#include <sstream>
#include <string>

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

ahfl::Owned<ahfl::ast::ExprSyntax> make_bool_expr(bool value) {
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::BoolLiteralExpr{value};
    e->text = value ? "true" : "false";
    return e;
}

ahfl::Owned<ahfl::ast::ExprSyntax> make_int_expr(const std::string &spelling,
                                                 std::int64_t value) {
    auto lit = ahfl::make_owned<ahfl::ast::IntegerSyntax>();
    lit->spelling = spelling;
    lit->value = value;
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::IntegerLiteralExpr{std::move(lit)};
    e->text = spelling;
    return e;
}

void test_wildcard_printer_shape() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = true;

    std::ostringstream out;
    ahfl::print_decreases_clause(clause, out);
    const auto text = out.str();

    check(text.find("decreases") != std::string::npos,
          "wildcard printer emits 'decreases' header");
    check(text.find("wildcard: true") != std::string::npos,
          "wildcard printer emits wildcard flag");
}

void test_explicit_printer_shape() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(make_int_expr("42", 42));
    clause.terms.push_back(make_bool_expr(true));

    std::ostringstream out;
    ahfl::print_decreases_clause(clause, out);
    const auto text = out.str();

    check(text.find("wildcard: false") != std::string::npos,
          "explicit printer emits wildcard=false");
    check(text.find("term 0") != std::string::npos, "explicit printer labels first term");
    check(text.find("term 1") != std::string::npos, "explicit printer labels second term");
    check(text.find("integer 42") != std::string::npos, "explicit printer prints integer term");
    check(text.find("bool true") != std::string::npos, "explicit printer prints bool term");
}

void test_empty_explicit_printer() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = false;

    std::ostringstream out;
    ahfl::print_decreases_clause(clause, out);
    const auto text = out.str();

    check(text.find("decreases") != std::string::npos, "empty explicit still emits header");
    check(text.find("wildcard: false") != std::string::npos,
          "empty explicit still emits wildcard=false");
    check(text.find("term") == std::string::npos,
          "empty explicit does not invent term rows");
}

void test_base_indent_applied() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = true;

    std::ostringstream out;
    ahfl::print_decreases_clause(clause, out, /*base_indent=*/3);
    const auto text = out.str();

    // Base indent of 3 means the top-level "decreases" line has 3*2 = 6
    // leading spaces (AstPrinter uses 2-space per indent unit).
    check(text.rfind("      decreases", 0) == 0,
          "base_indent=3 prefixes header with 6 spaces");
}

} // namespace

int main() {
    test_wildcard_printer_shape();
    test_explicit_printer_shape();
    test_empty_explicit_printer();
    test_base_indent_applied();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

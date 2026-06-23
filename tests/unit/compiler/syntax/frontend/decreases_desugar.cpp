// Standalone desugar tests for DecreasesClauseSyntax (R-09: no DeclKind dispatch).
//
// Exercises desugar_decreases_clause:
//   * wildcard → clears stray terms, returns true when it did work
//   * explicit → drops nullptr entries + consecutive duplicate spellings

#include "ahfl/compiler/frontend/frontend.hpp"

#include <cstdio>
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

ahfl::Owned<ahfl::ast::ExprSyntax> make_int(const std::string &s, std::int64_t v) {
    auto lit = ahfl::make_owned<ahfl::ast::IntegerSyntax>();
    lit->spelling = s;
    lit->value = v;
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::IntegerLiteralExpr{std::move(lit)};
    e->text = s;
    return e;
}

ahfl::Owned<ahfl::ast::ExprSyntax> make_bool(bool value) {
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::BoolLiteralExpr{value};
    e->text = value ? "true" : "false";
    return e;
}

void test_wildcard_clears_stray_terms() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = true;
    clause.terms.push_back(make_int("1", 1));
    clause.terms.push_back(make_int("2", 2));

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(changed, "wildcard with stray terms returns changed=true");
    check(clause.is_wildcard, "wildcard flag preserved after desugar");
    check(clause.terms.empty(), "stray terms cleared for wildcard form");
}

void test_wildcard_noop() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = true;

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(!changed, "wildcard w/o stray terms returns changed=false");
    check(clause.terms.empty(), "wildcard no-op stays empty");
}

void test_drops_nullptr_entries() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(nullptr);
    clause.terms.push_back(make_int("7", 7));
    clause.terms.push_back(nullptr);
    clause.terms.push_back(make_bool(true));
    clause.terms.push_back(nullptr);

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(changed, "nullptr-contaminated clause returns changed=true");
    check(clause.terms.size() == 2, "nullptr entries removed");
    // First surviving term: integer with spelling "7"
    check(clause.terms[0] && clause.terms[0]->text == "7",
          "first surviving term is integer 7");
    check(clause.terms[1] && clause.terms[1]->text == "true",
          "second surviving term is bool true");
}

void test_drops_consecutive_duplicates() {
    ahfl::ast::DecreasesClauseSyntax clause;
    // x, x, y, x, y, y → x, y, x, y  (only consecutive duplicates collapse)
    clause.terms.push_back(make_int("1", 1));
    clause.terms.push_back(make_int("1", 1)); // dup
    clause.terms.push_back(make_int("2", 2));
    clause.terms.push_back(make_int("1", 1)); // non-consecutive dup, kept
    clause.terms.push_back(make_int("2", 2));
    clause.terms.push_back(make_int("2", 2)); // dup

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(changed, "consecutive duplicate clause returns changed=true");
    check(clause.terms.size() == 4, "consecutive duplicates collapsed (n=4)");
    check(clause.terms[0]->text == "1", "dedup order 0");
    check(clause.terms[1]->text == "2", "dedup order 1");
    check(clause.terms[2]->text == "1", "dedup order 2 – non-consecutive kept");
    check(clause.terms[3]->text == "2", "dedup order 3");
}

void test_clean_clause_is_noop() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(make_int("1", 1));
    clause.terms.push_back(make_bool(false));
    clause.terms.push_back(make_int("3", 3));

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(!changed, "already-clean clause returns changed=false");
    check(clause.terms.size() == 3, "clean clause keeps all 3 terms");
}

void test_empty_explicit_is_noop() {
    ahfl::ast::DecreasesClauseSyntax clause;
    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(!changed, "empty explicit clause returns changed=false");
}

} // namespace

int main() {
    test_wildcard_clears_stray_terms();
    test_wildcard_noop();
    test_drops_nullptr_entries();
    test_drops_consecutive_duplicates();
    test_clean_clause_is_noop();
    test_empty_explicit_is_noop();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

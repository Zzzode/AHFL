// Three-way symmetry test for DecreasesClauseSyntax.
//
// Coverage:
//   * formatter output pinning (exact strings) – golden-style assertions.
//   * desugar → formatter idempotence: format(desugar(c)) == format(c).
//   * printer outline and formatter surface text both reflect structural
//     change introduced by desugar (e.g. duplicate term disappears from both).
//   * round-trip: build a clause, format it to surface text, assert the
//     formatted output matches what the grammar-level formatter helper
//     reconstructs from an equivalent manually-built clone.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "tooling/formatter/formatter.hpp"

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

ahfl::Owned<ahfl::ast::ExprSyntax> make_int(const std::string &s, std::int64_t v) {
    auto lit = ahfl::make_owned<ahfl::ast::IntegerSyntax>();
    lit->spelling = s;
    lit->value = v;
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::IntegerLiteralExpr{std::move(lit)};
    e->text = s;
    return e;
}

ahfl::Owned<ahfl::ast::ExprSyntax> make_path(const std::string &root,
                                              const std::vector<std::string> &members = {}) {
    auto p = ahfl::make_owned<ahfl::ast::PathSyntax>();
    p->root_kind = ahfl::ast::PathRootKind::Identifier;
    p->root_name = root;
    p->members = members;
    auto e = ahfl::make_owned<ahfl::ast::ExprSyntax>();
    e->node = ahfl::ast::PathExpr{std::move(p)};
    e->text = root;
    return e;
}

// ----- golden (exact-string) formatter pinning ----------------------------

void test_formatter_wildcard_golden() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.is_wildcard = true;
    const auto out = ahfl::formatter::format_decreases_clause(clause);
    const std::string expected = "decreases *;";
    check(out == expected, "golden.wildcard exact string: 'decreases *;'");
}

void test_formatter_empty_explicit_golden() {
    ahfl::ast::DecreasesClauseSyntax clause;
    const auto out = ahfl::formatter::format_decreases_clause(clause);
    const std::string expected = "decreases ();";
    check(out == expected, "golden.empty_exact: 'decreases ();'");
}

void test_formatter_multi_term_golden() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(make_int("42", 42));
    clause.terms.push_back(make_path("depth"));
    clause.terms.push_back(make_path("ctx", {"step"}));

    const auto out = ahfl::formatter::format_decreases_clause(clause);
    const std::string expected = "decreases (42, depth, ctx.step);";
    check(out == expected, "golden.multi_term: 'decreases (42, depth, ctx.step);'");
}

// ----- desugar → formatter idempotence ------------------------------------

void test_desugar_formatter_idempotence() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(make_int("1", 1));
    clause.terms.push_back(make_int("1", 1)); // consecutive duplicate
    clause.terms.push_back(make_path("x"));
    clause.terms.push_back(nullptr);           // nullptr contaminant
    clause.terms.push_back(make_path("y"));

    const std::string before = ahfl::formatter::format_decreases_clause(clause);

    // Canonicalise.  The duplicate and the nullptr both disappear.
    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(changed, "idempotence driver: changed=true expected");

    const std::string after = ahfl::formatter::format_decreases_clause(clause);

    // Second pass must be a no-op → text must stay identical.
    const bool changed2 = ahfl::desugar_decreases_clause(clause);
    check(!changed2, "idempotence: second desugar is a no-op");
    const std::string after2 = ahfl::formatter::format_decreases_clause(clause);
    check(after == after2, "idempotence: formatter output identical on 2nd pass");

    // Structural sanity: the cleaned form has fewer terms than the raw form.
    const std::string canonical = "decreases (1, x, y);";
    check(after == canonical, "idempotence: canonical text matches golden");
    // And the raw form was definitely different (defensive: catches bugs where
    // desugar silently becomes a no-op).
    check(before != after, "idempotence: raw text differs from canonical");
}

// ----- printer outline ↔ formatter surface symmetry -----------------------

void test_printer_and_formatter_agree_on_shape() {
    ahfl::ast::DecreasesClauseSyntax clause;
    clause.terms.push_back(make_int("3", 3));
    clause.terms.push_back(make_int("3", 3));
    clause.terms.push_back(make_path("n"));

    std::ostringstream printer_before;
    ahfl::print_decreases_clause(clause, printer_before);
    const auto fmt_before = ahfl::formatter::format_decreases_clause(clause);

    const bool changed = ahfl::desugar_decreases_clause(clause);
    check(changed, "symmetry driver: changed=true expected");

    std::ostringstream printer_after;
    ahfl::print_decreases_clause(clause, printer_after);
    const auto fmt_after = ahfl::formatter::format_decreases_clause(clause);

    // Both representations must see the duplicate disappear.
    const auto pb = printer_before.str();
    const auto pa = printer_after.str();
    check(pb.find("term 2") != std::string::npos, "printer has 3 terms before");
    check(pa.find("term 2") == std::string::npos, "printer has 2 terms after");
    check(fmt_before.find("3, 3, n") != std::string::npos,
          "formatter carries duplicate before");
    check(fmt_after == "decreases (3, n);",
          "formatter reflects deduped form after");
}

// ----- manual "round-trip" via program → reformat text symmetry -----------
//
// We build two semantically identical clauses from scratch and assert that
// the formatter produces byte-identical output for them.  This mirrors the
// intent of a full parse → print → reformat round-trip, but without needing
// to modify the parser grammar (a deliberate decoupling: DecreasesClauseSyntax
// is a syntax fragment first, surfaced by the grammar in a later pass).

void test_format_stability_semantically_equal() {
    auto build_a = [] {
        ahfl::ast::DecreasesClauseSyntax c;
        c.terms.push_back(make_int("10", 10));
        c.terms.push_back(make_path("foo"));
        return c;
    };
    auto build_b = [] {
        ahfl::ast::DecreasesClauseSyntax c;
        // Identical semantic content, re-ordered construction shouldn't matter
        // (but construction order here obviously does — we prove independence
        // only from trivial allocator state).
        c.terms.push_back(make_int("10", 10));
        c.terms.push_back(make_path("foo"));
        return c;
    };
    const auto a = ahfl::formatter::format_decreases_clause(build_a());
    const auto b = ahfl::formatter::format_decreases_clause(build_b());
    check(a == b, "round_trip.semantically_equal_clauses_format_identically");
    check(a == "decreases (10, foo);", "round_trip.canonical_ground_truth");
}

} // namespace

int main() {
    test_formatter_wildcard_golden();
    test_formatter_empty_explicit_golden();
    test_formatter_multi_term_golden();
    test_desugar_formatter_idempotence();
    test_printer_and_formatter_agree_on_shape();
    test_format_stability_semantically_equal();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

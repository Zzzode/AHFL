// Smoke tests for the live Trait / Impl diagnostic code set.
//
// Each covered code is emitted through the production DiagnosticBuilder API
// and then checked for:
//   1. The fully qualified code string (e.g. "typecheck.AMBIGUOUS_TRAIT_IMPL").
//   2. An exact formatted message that ties placeholder positions to the
//      AST-level field names used in the trait/impl nodes (trait_name,
//      impl_type, bound_trait, bound_type).
//
// Semantic placeholder ↔ AST-field equivalence:
//
//   impl_type            ↔ ImplDecl.target_type                (ast.hpp)
//   trait_name           ↔ TraitDecl.name / ImplDecl.trait_ref (as NamedType)
//   bound_type           ↔ WhereClauseConstraint.subject
//   bound_trait          ↔ WhereClauseConstraint.bounds[i]

#include "ahfl/base/support/diagnostics.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::Diagnostic;
using ahfl::DiagnosticBag;
using ahfl::DiagnosticCategory;
using ahfl::to_string;

namespace ec = ahfl::error_codes;
namespace msg = ahfl::messages;

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

struct Case {
    std::string name;
    bool passed{false};
    std::string detail;
};

std::vector<Case> g_cases;

void record_case(std::string name, bool passed, std::string detail = "") {
    g_cases.push_back(Case{std::move(name), passed, std::move(detail)});
}

const Diagnostic *find_code(const DiagnosticBag &bag, std::string_view code) {
    const auto &entries = bag.entries();
    auto it = std::find_if(
        entries.begin(), entries.end(), [code](const Diagnostic &d) {
            return d.code.has_value() && *d.code == code;
        });
    return it == entries.end() ? nullptr : &*it;
}

bool check_code(const DiagnosticBag &bag,
                std::string_view full_code,
                std::string_view expected_message) {
    const auto *d = find_code(bag, full_code);
    if (d == nullptr) {
        record_case("code_present:" + std::string(full_code),
                    false,
                    "missing diagnostic with code '" + std::string(full_code) + "'");
        return false;
    }
    record_case("code_present:" + std::string(full_code), true);

    if (d->message != expected_message) {
        record_case("message_exact:" + std::string(full_code),
                    false,
                    "expected '" + std::string(expected_message) +
                        "' got '" + d->message + "'");
        return false;
    }
    record_case("message_exact:" + std::string(full_code), true);
    return true;
}

// ---------------------------------------------------------------------------
// Individual diagnostic smoke cases
// ---------------------------------------------------------------------------

// 1. AMBIGUOUS_TRAIT_IMPL — placeholders: impl_type, trait_name
void test_ambiguous_trait_impl() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::AmbiguousTraitImpl)
        .message(msg::typecheck::AmbiguousTraitImpl,
                 "Parser<Json>",     // impl_type
                 "AsValue")          // trait_name
        .emit();

    const std::string expected =
        "multiple trait implementations match for type 'Parser<Json>' "
        "and trait 'AsValue'";
    check_code(bag, "typecheck.AMBIGUOUS_TRAIT_IMPL", expected);
}

// 2. TRAIT_BOUND_NOT_SATISFIED — placeholders: bound_type, bound_trait, impl_type
void test_trait_bound_not_satisfied() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::TraitBoundNotSatisfied)
        .message(msg::typecheck::TraitBoundNotSatisfied,
                 "Request",         // bound_type — the type carrying the bound
                 "Authenticated",   // bound_trait — the unsatisfied trait bound
                 "GuestRequest")    // impl_type  — the concrete type we checked
        .emit();

    const std::string expected =
        "trait bound 'Request: Authenticated' is not satisfied by type "
        "'GuestRequest'";
    check_code(bag, "typecheck.TRAIT_BOUND_NOT_SATISFIED", expected);
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int run_all() {
    test_ambiguous_trait_impl();
    test_trait_bound_not_satisfied();

    std::size_t passed = 0;
    std::size_t total = g_cases.size();
    for (const auto &c : g_cases) {
        if (c.passed) {
            ++passed;
        } else {
            std::cerr << "FAIL: " << c.name;
            if (!c.detail.empty()) {
                std::cerr << " -- " << c.detail;
            }
            std::cerr << '\n';
        }
    }

    std::cout << passed << "/" << total << " diagnostic smoke assertions passed\n";
    return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main() { return run_all(); }

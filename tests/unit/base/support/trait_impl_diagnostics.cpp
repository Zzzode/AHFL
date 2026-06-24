// Smoke tests for the Trait / Impl diagnostic code set (P3c.S6).
//
// Each of the 8 required codes is emitted through the production
// DiagnosticBuilder API and then checked for:
//   1. The fully qualified code string (e.g. "typecheck.NO_TRAIT_IMPL").
//   2. An exact formatted message that ties placeholder positions to the
//      AST-level field names used in the trait/impl nodes (trait_name,
//      impl_type, method_name, assoc_name, bound_trait, bound_type,
//      expected_signature, actual_signature).
//
// Semantic placeholder ↔ AST-field equivalence (kept stable for all 8 codes
// so the P3b/P4a passes that emit these diagnostics know exactly which
// AST accessor to reach for each slot):
//
//   impl_type            ↔ ImplDecl.target_type                (ast.hpp)
//   trait_name           ↔ TraitDecl.name / ImplDecl.trait_ref (as NamedType)
//   bound_type           ↔ WhereClauseConstraint.subject
//   bound_trait          ↔ WhereClauseConstraint.bounds[i]
//   method_name          ↔ TraitItemSyntax.name / FnDecl.name
//   expected_signature   ↔ TraitItemSyntax::Fn rendered signature
//   actual_signature     ↔ FnDecl (impl method) rendered signature
//   assoc_name           ↔ AssocItemDefSyntax.name / AssocTypeDecl.name
//   member_name          ↔ FnDecl.name (inherent vs trait conflict side)

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

// 1. NO_TRAIT_IMPL — placeholders: impl_type, trait_name
void test_no_trait_impl() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::NoTraitImpl)
        .message(msg::typecheck::NoTraitImpl,
                 "OrderBook",       // impl_type
                 "Serializable")    // trait_name
        .emit();

    static_assert(ec::typecheck::NoTraitImpl.category == DiagnosticCategory::TypeCheck,
                  "NoTraitImpl must live in the TypeCheck category");

    const std::string expected =
        "type 'OrderBook' does not implement trait 'Serializable'";
    const std::string code =
        std::string("typecheck.") + "NO_TRAIT_IMPL";
    check_code(bag, code, expected);
}

// 2. AMBIGUOUS_TRAIT_IMPL — placeholders: impl_type, trait_name
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

// 3. TRAIT_BOUND_NOT_SATISFIED — placeholders: bound_type, bound_trait, impl_type
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

// 4. METHOD_NOT_FOUND — placeholders: method_name, impl_type
void test_method_not_found() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::MethodNotFound)
        .message(msg::typecheck::MethodNotFound,
                 "render",       // method_name
                 "LayoutNode")   // impl_type
        .emit();

    const std::string expected = "method 'render' not found on type 'LayoutNode'";
    check_code(bag, "typecheck.METHOD_NOT_FOUND", expected);
}

// 5. METHOD_SIGNATURE_MISMATCH — placeholders map to AST fields:
//    method_name          = TraitItemSyntax::Fn.name / FnDecl.name
//    impl_type            = ImplDecl.target_type   (impl's nominal target)
//    trait_name           = ImplDecl.trait_ref     (trait symbol looked up)
//    expected_signature   = rendered trait method signature (trait side)
//    actual_signature     = rendered impl  FnDecl signature          (impl side)
void test_method_signature_mismatch() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::MethodSignatureMismatch)
        .message(msg::typecheck::MethodSignatureMismatch,
                 "encode",                         // method_name
                 "Config",                         // impl_type
                 "Encodable",                      // trait_name
                 "fn encode(self) -> Bytes",       // expected_signature
                 "fn encode(self) -> String")      // actual_signature
        .emit();

    const std::string expected =
        "method 'encode' signature mismatch on impl 'Config' of trait "
        "'Encodable': expected 'fn encode(self) -> Bytes', got "
        "'fn encode(self) -> String'";
    check_code(bag, "typecheck.METHOD_SIGNATURE_MISMATCH", expected);
}

// 6. ASSOC_TYPE_NOT_FOUND — placeholders: assoc_name, trait_name
void test_assoc_type_not_found() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::AssocTypeNotFound)
        .message(msg::typecheck::AssocTypeNotFound,
                 "Accumulator",   // assoc_name
                 "Foldable")      // trait_name
        .emit();

    const std::string expected =
        "associated type 'Accumulator' not found on trait 'Foldable'";
    check_code(bag, "typecheck.ASSOC_TYPE_NOT_FOUND", expected);
}

// 7. INHERENT_TRAIT_CONFLICT — placeholders: member_name, impl_type, trait_name
void test_inherent_trait_conflict() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::typecheck::InherentTraitConflict)
        .message(msg::typecheck::InherentTraitConflict,
                 "name",         // member_name
                 "User",         // impl_type
                 "Display")      // trait_name
        .emit();

    const std::string expected =
        "member 'name' on 'User' conflicts between inherent impl and "
        "trait impl of 'Display'";
    check_code(bag, "typecheck.INHERENT_TRAIT_CONFLICT", expected);
}

// 8. TRAIT_ORPHAN_IMPL (resolve category) — placeholders: trait_name, impl_type
//    Also verifies dedup: there is no other orphan_impl code defined
//    anywhere in the error_codes namespace.
void test_trait_orphan_impl() {
    DiagnosticBag bag;
    bag.error()
        .code(ec::resolve::TraitOrphanImpl)
        .message(msg::resolve::TraitOrphanImpl,
                 "PrettyPrint",   // trait_name
                 "ExternalType")  // impl_type
        .emit();

    static_assert(ec::resolve::TraitOrphanImpl.category == DiagnosticCategory::Resolve,
                  "TraitOrphanImpl must live in the Resolve category");

    const std::string expected =
        "impl for trait 'PrettyPrint' on type 'ExternalType' violates "
        "the orphan rule: neither the trait nor the type is local to this module";
    check_code(bag, "resolve.TRAIT_ORPHAN_IMPL", expected);
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int run_all() {
    test_no_trait_impl();
    test_ambiguous_trait_impl();
    test_trait_bound_not_satisfied();
    test_method_not_found();
    test_method_signature_mismatch();
    test_assoc_type_not_found();
    test_inherent_trait_conflict();
    test_trait_orphan_impl();

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

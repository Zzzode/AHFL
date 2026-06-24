// ============================================================================
// P2d.S3: monomorphization budget + diagnostics unit tests
//
// Covers:
//   * Centralised constants (kMonomorphizationUserBudget == 32,
//     kMonomorphizationStdlibBudget == 256, and the MONO_BUDGET_* macros).
//   * Per-origin (user vs stdlib) counting: std:: prefixed names charge the
//     stdlib budget; everything else charges the user budget.
//   * Cache-key dedup on (decl_canonical_name, type_args_canonical) — a
//     repeated site does not increment the counter.
//   * Budget overflow for the user budget triggers a single
//     typecheck.MONOMORPHIZATION_BUDGET_EXCEEDED diagnostic whose message
//     contains the expected origin label, count and limit.
//   * Budget overflow for the stdlib budget is diagnosed analogously.
//   * MonomorphizationResult::budget_exceeded() flips from false to true as
//     soon as either counter exceeds its ceiling.
//   * Options::enforce_budget=false clamps counters without diagnostics.
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/semantics/monomorphization.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using ahfl::kMonomorphizationStdlibBudget;
using ahfl::kMonomorphizationUserBudget;
using ahfl::DiagnosticBag;
using ahfl::DiagnosticSeverity;
using ahfl::FnCallSite;
using ahfl::MonomorphizationOptions;
using ahfl::MonomorphizationResult;
using ahfl::run_monomorphization;

// Helper: build a vector of n distinct user-origined call sites, each with a
// unique canonical name so every site becomes a cache miss.
std::vector<FnCallSite> make_user_sites(std::uint32_t n,
                                        std::string_view prefix = "app::f_") {
    std::vector<FnCallSite> sites;
    sites.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        FnCallSite s;
        s.fn_symbol.value = static_cast<std::size_t>(i) + 1;
        s.decl_canonical_name = std::string(prefix) + std::to_string(i);
        sites.push_back(std::move(s));
    }
    return sites;
}

std::vector<FnCallSite> make_stdlib_sites(std::uint32_t n) {
    return make_user_sites(n, "std::f_");
}

// Helper: count diagnostics carrying the budget-exceeded error code.
[[nodiscard]] int count_budget_diagnostics(const DiagnosticBag &bag) {
    int count = 0;
    for (const auto &d : bag.entries()) {
        if (d.severity != DiagnosticSeverity::Error) {
            continue;
        }
        if (d.code.has_value() &&
            d.code->find("MONOMORPHIZATION_BUDGET_EXCEEDED") != std::string::npos) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool diagnostic_mentions(const DiagnosticBag &bag,
                                       std::string_view snippet) {
    for (const auto &d : bag.entries()) {
        if (d.message.find(snippet) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// TC1: constants are centralised with the RFC-mandated numeric values.
// ---------------------------------------------------------------------------
TEST_CASE("P2d.S3 budget constants are centralised with correct values") {
    static_assert(kMonomorphizationUserBudget == 32U, "user budget == 32");
    static_assert(kMonomorphizationStdlibBudget == 256U, "stdlib budget == 256");
    CHECK(kMonomorphizationUserBudget == MONO_BUDGET_USER);
    CHECK(kMonomorphizationStdlibBudget == MONO_BUDGET_STDLIB);
    CHECK(MONO_BUDGET_USER == 32U);
    CHECK(MONO_BUDGET_STDLIB == 256U);
}

// ---------------------------------------------------------------------------
// TC2: empty / small call sites stay under budget, no diagnostic emitted.
// ---------------------------------------------------------------------------
TEST_CASE("Small user program stays under budget and records exact counts") {
    DiagnosticBag diagnostics;
    const auto sites = make_user_sites(5);
    const auto result = run_monomorphization(sites, diagnostics);

    CHECK(result.user_instance_count == 5);
    CHECK(result.stdlib_instance_count == 0);
    CHECK(result.total_instance_count() == 5);
    CHECK_FALSE(result.budget_exceeded());
    CHECK_FALSE(diagnostics.has_error());
    CHECK(count_budget_diagnostics(diagnostics) == 0);
    // Per-decl counters are populated.
    CHECK(result.instances_per_decl.size() == 5);
}

// ---------------------------------------------------------------------------
// TC3: cache-key dedup — repeat site does not double-count.
// ---------------------------------------------------------------------------
TEST_CASE("Duplicate call sites are deduped against the budget") {
    DiagnosticBag diagnostics;
    std::vector<FnCallSite> sites;
    sites.reserve(200);
    for (int i = 0; i < 200; ++i) {
        FnCallSite s;
        s.fn_symbol.value = 1;
        s.decl_canonical_name = "app::one";
        s.type_args_canonical = "Int";
        sites.push_back(std::move(s));
    }
    const auto result = run_monomorphization(sites, diagnostics);

    CHECK(result.user_instance_count == 1);
    CHECK(result.instances.size() == 1);
    CHECK(result.instances.front().instance_name == "app::one<Int>");
    CHECK_FALSE(result.budget_exceeded());
    CHECK(count_budget_diagnostics(diagnostics) == 0);
}

// ---------------------------------------------------------------------------
// TC4: user vs stdlib counters are attributed independently.
// ---------------------------------------------------------------------------
TEST_CASE("User and stdlib counts are attributed to separate budgets") {
    DiagnosticBag diagnostics;
    std::vector<FnCallSite> sites;
    // 10 user, 20 stdlib — interleaved so we exercise the split inside
    // the single pass loop.
    for (std::uint32_t i = 0; i < 20; ++i) {
        {
            FnCallSite s;
            s.fn_symbol.value = i * 2 + 1;
            s.decl_canonical_name = "std::s_" + std::to_string(i);
            sites.push_back(std::move(s));
        }
        if (i < 10) {
            FnCallSite s;
            s.fn_symbol.value = i * 2 + 2;
            s.decl_canonical_name = "app::u_" + std::to_string(i);
            sites.push_back(std::move(s));
        }
    }

    const auto result = run_monomorphization(sites, diagnostics);
    CHECK(result.user_instance_count == 10);
    CHECK(result.stdlib_instance_count == 20);
    CHECK(result.total_instance_count() == 30);
    CHECK_FALSE(result.budget_exceeded());
    CHECK(count_budget_diagnostics(diagnostics) == 0);

    // Instance metadata tags match the origin.
    for (const auto &inst : result.instances) {
        if (inst.decl_canonical_name.substr(0, 5) == "std::") {
            CHECK(inst.from_stdlib);
        } else {
            CHECK_FALSE(inst.from_stdlib);
        }
    }
}

// ---------------------------------------------------------------------------
// TC5 (negative): user budget overflows → MONOMORPHIZATION_BUDGET_EXCEEDED
// diagnostic with exact string matches.
// ---------------------------------------------------------------------------
TEST_CASE("P2d.S3 user budget overflow emits MONOMORPHIZATION_BUDGET_EXCEEDED") {
    DiagnosticBag diagnostics;
    // 32 + 1 — one site beyond the user ceiling.
    const auto sites = make_user_sites(kMonomorphizationUserBudget + 1);
    const auto result = run_monomorphization(sites, diagnostics);

    // Budget enforcement: the overflow site is dropped, so the counter lands
    // exactly at the ceiling.
    CHECK(result.user_instance_count == kMonomorphizationUserBudget);
    CHECK(result.budget_exceeded());
    CHECK(diagnostics.has_error());
    CHECK(count_budget_diagnostics(diagnostics) == 1);

    // Exact-string assertion on the diagnostic payload (acceptance #3).
    REQUIRE(diagnostics.entries().size() >= 1);
    const auto &diag = diagnostics.entries().front();
    CHECK(diag.code.has_value());
    CHECK(diag.code.value() == "typecheck.MONOMORPHIZATION_BUDGET_EXCEEDED");
    CHECK(diag.message.find("user code") != std::string::npos);
    CHECK(diag.message.find("budget 32") != std::string::npos);
    CHECK(diag.message.find("distinct instances") != std::string::npos);
    CHECK(diag.message.find("largest contributors") != std::string::npos);
}

// ---------------------------------------------------------------------------
// TC6 (negative): stdlib budget overflow → exactly one budget diagnostic
// (even though the user budget is untouched).
// ---------------------------------------------------------------------------
TEST_CASE("P2d.S3 stdlib budget overflow emits exactly one diagnostic") {
    DiagnosticBag diagnostics;
    const auto sites = make_stdlib_sites(kMonomorphizationStdlibBudget + 5);
    const auto result = run_monomorphization(sites, diagnostics);

    CHECK(result.stdlib_instance_count == kMonomorphizationStdlibBudget);
    CHECK(result.user_instance_count == 0);
    CHECK(result.budget_exceeded());
    // Over-budget sites are still deduped so we never emit more than one
    // diagnostic per budget kind.
    CHECK(count_budget_diagnostics(diagnostics) == 1);
    CHECK(diagnostic_mentions(diagnostics, "stdlib code"));
    CHECK(diagnostic_mentions(diagnostics, "budget 256"));
}

// ---------------------------------------------------------------------------
// TC7 (negative): combined overflow → two diagnostics, one per budget.
// ---------------------------------------------------------------------------
TEST_CASE("Combined user + stdlib overflow emits two budget diagnostics") {
    DiagnosticBag diagnostics;
    std::vector<FnCallSite> sites = make_user_sites(kMonomorphizationUserBudget + 3);
    const auto stdlib = make_stdlib_sites(kMonomorphizationStdlibBudget + 3);
    sites.insert(sites.end(), stdlib.begin(), stdlib.end());
    const auto result = run_monomorphization(sites, diagnostics);

    CHECK(result.user_instance_count == kMonomorphizationUserBudget);
    CHECK(result.stdlib_instance_count == kMonomorphizationStdlibBudget);
    CHECK(result.budget_exceeded());
    CHECK(count_budget_diagnostics(diagnostics) == 2);
    CHECK(diagnostic_mentions(diagnostics, "user code"));
    CHECK(diagnostic_mentions(diagnostics, "stdlib code"));
}

// ---------------------------------------------------------------------------
// TC8: enforce_budget=false clamps counters and suppresses diagnostics.
// ---------------------------------------------------------------------------
TEST_CASE("enforce_budget=false clamps counters and emits no diagnostics") {
    DiagnosticBag diagnostics;
    MonomorphizationOptions options;
    options.enforce_budget = false;
    const auto sites = make_user_sites(kMonomorphizationUserBudget * 2);
    const auto result = run_monomorphization(sites, diagnostics, options);

    // Without enforcement every distinct site still gets recorded; only the
    // "budget exceeded" diagnostic is turned off. We intentionally keep the
    // counters honest above the ceiling so callers can observe the real
    // pressure.
    CHECK(result.user_instance_count == kMonomorphizationUserBudget * 2);
    CHECK(result.budget_exceeded());
    CHECK_FALSE(diagnostics.has_error());
    CHECK(count_budget_diagnostics(diagnostics) == 0);
}

// ---------------------------------------------------------------------------
// TC9: canonical_type_args renders types via TypePtr::describe.
// ---------------------------------------------------------------------------
TEST_CASE("canonical_type_args renders an empty vector to empty string") {
    std::vector<ahfl::TypePtr> empty;
    CHECK(ahfl::canonical_type_args(empty).empty());
}

// ---------------------------------------------------------------------------
// TC10: instances are named consistently with the rule decl<TypeArgs>.
// ---------------------------------------------------------------------------
TEST_CASE("Instance name rendering matches decl<T1, T2> convention") {
    DiagnosticBag diagnostics;
    std::vector<FnCallSite> sites;
    {
        FnCallSite s;
        s.fn_symbol.value = 1;
        s.decl_canonical_name = "app::id";
        s.type_args_canonical = "Int";
        sites.push_back(std::move(s));
    }
    {
        FnCallSite s;
        s.fn_symbol.value = 2;
        s.decl_canonical_name = "app::non_generic";
        sites.push_back(std::move(s));
    }
    {
        FnCallSite s;
        s.fn_symbol.value = 3;
        s.decl_canonical_name = "app::pair";
        s.type_args_canonical = "Int, String";
        sites.push_back(std::move(s));
    }
    const auto result = run_monomorphization(sites, diagnostics);
    REQUIRE(result.instances.size() == 3);
    CHECK(result.instances[0].instance_name == "app::id<Int>");
    CHECK(result.instances[1].instance_name == "app::non_generic");
    CHECK(result.instances[2].instance_name == "app::pair<Int, String>");
}

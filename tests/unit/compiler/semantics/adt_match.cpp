#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// P1b ADT match typecheck tests.
//
// The match typecheck pass (RFC P1 / §1.6) implements:
//   * scrutinee must be an enum
//   * variant pattern coverage -> exhaustiveness
//   * payload binding types (binding slot positions to payload types)
//   * arm body type unification
//
// These tests drive the full parse -> resolve -> typecheck pipeline. Each
// TEST_CASE is self-contained and asserts either a clean typecheck or the
// presence of a specific MATCH_* diagnostic code.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string module_preamble() {
    return std::string{R"AHFL(
module adt_match;
)AHFL"};
}

// Run the full pipeline and return the typecheck result. On unexpected parse
// or resolve failures the diagnostic is surfaced via MESSAGE so a failing
// test reports an actionable cause rather than a silent boolean.
[[nodiscard]] ahfl::TypeCheckResult typecheck_source(const std::string &source) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("adt_match.ahfl", source);
    if (parse_result.has_errors()) {
        MESSAGE("parse errors detected");
        for (const auto &entry : parse_result.diagnostics.entries()) {
            MESSAGE("  parse: " << entry.message);
        }
    }
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        for (const auto &entry : resolve_result.diagnostics.entries()) {
            MESSAGE("  resolve: " << entry.message);
        }
    }
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto result = type_checker.check(*parse_result.program, resolve_result);
    if (result.has_errors() && !result.diagnostics.entries().empty()) {
        const auto &d = result.diagnostics.entries().front();
        MESSAGE("typecheck diagnostic: " << d.message);
    }
    return result;
}

[[nodiscard]] bool has_diagnostic_code(const ahfl::TypeCheckResult &result,
                                       std::string_view code_substring) {
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && entry.code->find(code_substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Match lives inside a flow state body. The harness below wraps a `match`
// expression inside a minimal agent + flow whose context exposes an
// enum-typed field, so the scrutinee `ctx.value` resolves to the enum type
// under test. The match result is bound to `r` and returned through the
// Response struct so the final-state handler satisfies the `must return`
// validation rule (the rule only inspects control flow, not types, so
// match-bearing diagnostics still surface from the typecheck pass).
[[nodiscard]] std::string wrap_in_flow(std::string_view enum_decls,
                                       std::string_view context_type,
                                       std::string_view default_value,
                                       std::string_view match_expr) {
    return module_preamble() + std::string{R"AHFL(
struct Response {
    value: Int;
}
)AHFL"} + std::string{enum_decls} + std::string{R"AHFL(
struct Context {
    value: )AHFL"} + std::string{context_type} + std::string{R"AHFL( = )AHFL"} +
           std::string{default_value} + std::string{R"AHFL(;
}

agent MatchAgent {
    input: Response;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for MatchAgent {
    state Done {
        let r = )AHFL"} + std::string{match_expr} + std::string{R"AHFL(;
        return Response { value: r };
    }
}
)AHFL"};
}

} // namespace

// ---------------------------------------------------------------------------
// Backward compatibility: a legacy payload-less enum still typechecks cleanly.
// This guards the P1 constraint that P1b must not break the existing 953.
// ---------------------------------------------------------------------------
TEST_CASE("payload-less enum still typechecks after P1b") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red => 0, Green => 1, Blue => 2 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// A payload enum declaration typechecks without diagnostics (the payload slot
// types are resolved into the EnumTypeInfo via build_enum_types).
// ---------------------------------------------------------------------------
TEST_CASE("payload enum declaration typechecks") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Maybe { Some(Int), None, }
)AHFL",
        "Maybe",
        "Maybe::None",
        // Catch-all via `_` so exhaustiveness is satisfied; payload binding
        // path is exercised only by the dedicated narrowing test below.
        "match ctx.value { _ => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Exhaustiveness: covering every variant of a payload-less enum compiles.
// ---------------------------------------------------------------------------
TEST_CASE("exhaustive match over payload-less enum compiles") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red => 1, Green => 2, Blue => 3 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Exhaustiveness: a missing variant reports MATCH_NOT_EXHAUSTIVE.
// ---------------------------------------------------------------------------
TEST_CASE("non-exhaustive match reports MATCH_NOT_EXHAUSTIVE") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        // Blue is not covered.
        "match ctx.value { Red => 1, Green => 2 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_NOT_EXHAUSTIVE"));
}

// ---------------------------------------------------------------------------
// Wildcard arm makes any match exhaustive (no MATCH_NOT_EXHAUSTIVE).
// ---------------------------------------------------------------------------
TEST_CASE("wildcard arm satisfies exhaustiveness") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red => 1, _ => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Binding-only arm (`x`) is irrefutable and satisfies exhaustiveness.
// ---------------------------------------------------------------------------
TEST_CASE("binding arm satisfies exhaustiveness") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { x => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Exhaustiveness over a payload enum: covering both Some and None compiles.
// ---------------------------------------------------------------------------
TEST_CASE("exhaustive match over payload enum compiles") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Maybe { Some(Int), None, }
)AHFL",
        "Maybe",
        "Maybe::None",
        "match ctx.value { Some(n) => n, None => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Narrowing: a variant pattern with a payload binding narrows the binding to
// the payload slot type. Body referencing the binding must type-check against
// that type (Int). Skipping a variant reports MATCH_NOT_EXHAUSTIVE.
// ---------------------------------------------------------------------------
TEST_CASE("variant pattern binds payload slot type") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Maybe { Some(Int), None, }
)AHFL",
        "Maybe",
        "Maybe::None",
        // `n` binds to Int (the Some payload slot). `n + 1` is well-typed.
        "match ctx.value { Some(n) => n + 1, None => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Arm body type unification: diverging arm body types report TYPE_MISMATCH.
// ---------------------------------------------------------------------------
TEST_CASE("diverging arm body types report mismatch") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        // First arm body is Int (1); second arm body is Bool (true) — diverges.
        "match ctx.value { Red => 1, Green => true, Blue => 3 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
}

// ---------------------------------------------------------------------------
// Scrutinee must be an enum. Matching an Int reports MATCH_SCRUTINEE_REQUIRES_ENUM.
// ---------------------------------------------------------------------------
TEST_CASE("non-enum scrutinee reports MATCH_SCRUTINEE_REQUIRES_ENUM") {
    // Provide an Int-typed context field and match against it.
    const auto source = wrap_in_flow(
        "",
        "Int",
        "0",
        "match ctx.value { _ => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_SCRUTINEE_REQUIRES_ENUM"));
}

// ---------------------------------------------------------------------------
// Unknown variant in a pattern reports MATCH_UNKNOWN_VARIANT.
// ---------------------------------------------------------------------------
TEST_CASE("unknown variant reports MATCH_UNKNOWN_VARIANT") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        // Purple is not a variant of Light.
        "match ctx.value { Light::Purple => 1, _ => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_UNKNOWN_VARIANT"));
}

// ---------------------------------------------------------------------------
// Variant payload arity mismatch reports MATCH_VARIANT_PAYLOAD_ARITY.
// ---------------------------------------------------------------------------
TEST_CASE("payload arity mismatch reports MATCH_VARIANT_PAYLOAD_ARITY") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Maybe { Some(Int), None, }
)AHFL",
        "Maybe",
        "Maybe::None",
        // Some expects 1 payload slot, pattern supplies 2.
        "match ctx.value { Some(a, b) => a, None => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_VARIANT_PAYLOAD_ARITY"));
}

// ---------------------------------------------------------------------------
// Multi-variant enum exhaustiveness: every named variant must be covered.
// ---------------------------------------------------------------------------
TEST_CASE("multi-variant payload enum exhaustiveness gap") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Result_ { Ok(Int), Err(Int), Initial, }
)AHFL",
        "Result_",
        "Result_::Initial",
        // Err/Initial not covered -> MATCH_NOT_EXHAUSTIVE.
        "match ctx.value { Ok(v) => v }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_NOT_EXHAUSTIVE"));
}

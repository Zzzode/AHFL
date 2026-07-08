#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>
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

[[nodiscard]] ahfl::ResolveResult resolve_source(const std::string &source) {
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
    return resolver.resolve(*parse_result.program);
}

[[nodiscard]] bool has_diagnostic_code(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view code_substring) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && entry.code->find(code_substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_diagnostic_code(const ahfl::TypeCheckResult &result,
                                       std::string_view code_substring) {
    return has_diagnostic_code(result.diagnostics, code_substring);
}

[[nodiscard]] const ahfl::Diagnostic *
find_diagnostic_with_code(const ahfl::DiagnosticBag &diagnostics, std::string_view code_substring) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && entry.code->find(code_substring) != std::string::npos) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t diagnostic_count_with_code(const ahfl::DiagnosticBag &diagnostics,
                                                     std::string_view code_substring) {
    return static_cast<std::size_t>(std::count_if(diagnostics.entries().begin(),
                                                  diagnostics.entries().end(),
                                                  [&](const ahfl::Diagnostic &entry) {
                                                      return entry.code.has_value() &&
                                                             entry.code->find(code_substring) !=
                                                                 std::string::npos;
                                                  }));
}

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view message_substring) {
    return std::any_of(diagnostics.entries().begin(),
                       diagnostics.entries().end(),
                       [&](const ahfl::Diagnostic &entry) {
                           if (entry.message.find(message_substring) != std::string::npos) {
                               return true;
                           }
                           return std::any_of(entry.related.begin(),
                                              entry.related.end(),
                                              [&](const ahfl::Diagnostic::Related &related) {
                                                  return related.message.find(message_substring) !=
                                                         std::string::npos;
                                              });
                       });
}

[[nodiscard]] bool related_contains(const ahfl::Diagnostic &diagnostic,
                                    std::string_view message_substring) {
    return std::any_of(diagnostic.related.begin(),
                       diagnostic.related.end(),
                       [&](const ahfl::Diagnostic::Related &related) {
                           return related.message.find(message_substring) != std::string::npos;
                       });
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
)AHFL"} + std::string{enum_decls} +
           std::string{R"AHFL(
struct Context {
    value: )AHFL"} +
           std::string{context_type} + std::string{R"AHFL( = )AHFL"} + std::string{default_value} +
           std::string{R"AHFL(;
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
        let r = )AHFL"} +
           std::string{match_expr} + std::string{R"AHFL(;
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
// Exhaustiveness: a missing variant reports MATCH_MISSING_PATTERNS.
// ---------------------------------------------------------------------------
TEST_CASE("non-exhaustive match reports MATCH_MISSING_PATTERNS with related notes") {
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
    const auto *diagnostic =
        find_diagnostic_with_code(result.diagnostics, "MATCH_MISSING_PATTERNS");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity == ahfl::DiagnosticSeverity::Error);
    CHECK(diagnostic->message.find("Blue") != std::string::npos);
    CHECK(related_contains(*diagnostic, "enum 'adt_match::Light' declared here"));
    CHECK(related_contains(*diagnostic, "missing variant 'Blue' declared here"));
}

// ---------------------------------------------------------------------------
// Wildcard arm makes any match exhaustive.
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

TEST_CASE("wildcard followed by variants reports overlap and unreachable warnings") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { _ => 0, Red => 1, Green => 2 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 2);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 2);
    const auto *unreachable =
        find_diagnostic_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM");
    REQUIRE(unreachable != nullptr);
    CHECK(unreachable->severity == ahfl::DiagnosticSeverity::Warning);
    CHECK(related_contains(*unreachable, "previously covered by arm #1"));
}

TEST_CASE("duplicate variant arm reports overlap and unreachable warnings") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red => 1, Red => 2, Green => 3, Blue => 4 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 1);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 1);
    const auto *overlap = find_diagnostic_with_code(result.diagnostics, "MATCH_OVERLAP");
    REQUIRE(overlap != nullptr);
    CHECK(overlap->severity == ahfl::DiagnosticSeverity::Warning);
    CHECK(overlap->message.find("arm #1") != std::string::npos);
    CHECK(related_contains(*overlap, "overlaps with previous arm #1"));
}

TEST_CASE("catch-all after full variant coverage reports unreachable warning") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red => 1, Green => 2, Blue => 3, _ => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 1);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 3);
    const auto *unreachable =
        find_diagnostic_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM");
    REQUIRE(unreachable != nullptr);
    CHECK(related_contains(*unreachable, "previously covered by arm #1"));
    CHECK(related_contains(*unreachable, "previously covered by arm #2"));
    CHECK(related_contains(*unreachable, "previously covered by arm #3"));
}

TEST_CASE("guarded arm does not satisfy exhaustiveness") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red if true => 1, Green => 2, Blue => 3 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    const auto *diagnostic =
        find_diagnostic_with_code(result.diagnostics, "MATCH_MISSING_PATTERNS");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message.find("Red") != std::string::npos);
    CHECK(related_contains(*diagnostic, "missing variant 'Red' declared here"));
}

TEST_CASE("guarded arm does not make later same variant unreachable") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red if true => 1, Red => 2, Green => 3, Blue => 4 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 0);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 1);
}

TEST_CASE("empty coverage pattern does not overlap wildcard") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { 1 => 0, _ => 1 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 0);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 0);
}

TEST_CASE("match guard must be Bool") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red if 1 => 1, Red => 2, Green => 3, Blue => 4 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
    CHECK(diagnostics_contain(result.diagnostics, "match guard must have type Bool"));
}

TEST_CASE("match guard must be pure") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Light { Red, Green, Blue, }

fn nondet_guard() -> Bool effect Nondet {
    return true;
}
)AHFL",
        "Light",
        "Light::Red",
        "match ctx.value { Red if nondet_guard() => 1, Red => 2, Green => 3, Blue => 4 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "NON_PURE_EXPRESSION"));
    CHECK(diagnostics_contain(result.diagnostics, "match guard must be pure"));
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
// that type (Int). Skipping a variant reports MATCH_MISSING_PATTERNS.
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

TEST_CASE("nested enum payload match reports precise missing witness") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Flag { On, Off, }
enum MaybeFlag { Some(Flag), None, }
)AHFL",
        "MaybeFlag",
        "MaybeFlag::None",
        "match ctx.value { None => 0, Some(On) => 1 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    const auto *diagnostic =
        find_diagnostic_with_code(result.diagnostics, "MATCH_MISSING_PATTERNS");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message.find("Some(Off)") != std::string::npos);
    CHECK(related_contains(*diagnostic, "missing variant 'Some' declared here"));
}

TEST_CASE("nested enum payload exhaustive match compiles") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Flag { On, Off, }
enum MaybeFlag { Some(Flag), None, }
)AHFL",
        "MaybeFlag",
        "MaybeFlag::None",
        "match ctx.value { None => 0, Some(On) => 1, Some(Off) => 2 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

TEST_CASE("duplicate nested enum payload arm reports usefulness warnings") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Flag { On, Off, }
enum MaybeFlag { Some(Flag), None, }
)AHFL",
        "MaybeFlag",
        "MaybeFlag::None",
        "match ctx.value { Some(On) => 1, Some(On) => 2, Some(Off) => 3, None => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_UNREACHABLE_ARM") == 1);
    CHECK(diagnostic_count_with_code(result.diagnostics, "MATCH_OVERLAP") == 1);
}

TEST_CASE("qualified unknown nested enum payload variant reports diagnostic") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Flag { On, Off, }
enum MaybeFlag { Some(Flag), None, }
)AHFL",
        "MaybeFlag",
        "MaybeFlag::None",
        "match ctx.value { None => 0, Some(Flag::Missing) => 1, Some(_) => 2 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MATCH_UNKNOWN_VARIANT"));
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
    const auto source = wrap_in_flow("", "Int", "0", "match ctx.value { _ => 0 }");
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

TEST_CASE("enum variant name cannot shadow a module type name") {
    const auto source = module_preamble() + R"AHFL(
struct Data {
    code: Int;
}

enum Packet {
    Empty,
    Data { code: Int },
}
)AHFL";
    const auto result = resolve_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result.diagnostics, "VARIANT_NAME_SHADOWS_TYPE"));
}

TEST_CASE("struct variant duplicate field reports DUPLICATE_VARIANT_FIELD") {
    const auto source = module_preamble() + R"AHFL(
enum Packet {
    Data { code: Int, code: String },
}
)AHFL";
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "DUPLICATE_VARIANT_FIELD"));
}

TEST_CASE("struct variant pattern duplicate field reports DUPLICATE_VARIANT_FIELD") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data { code, code: _, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "DUPLICATE_VARIANT_FIELD"));
    CHECK_FALSE(has_diagnostic_code(result, "DUPLICATE_FIELD"));
}

TEST_CASE("struct variant pattern binds named fields") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data { code, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());

    const auto enum_it =
        std::find_if(result.typed_program.declarations.begin(),
                     result.typed_program.declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         const auto *info = std::get_if<ahfl::EnumTypeInfo>(&decl.payload);
                         return info != nullptr && info->canonical_name == "adt_match::Packet";
                     });
    REQUIRE(enum_it != result.typed_program.declarations.end());
    const auto &packet = std::get<ahfl::EnumTypeInfo>(enum_it->payload);
    const auto data = packet.find_variant("Data");
    REQUIRE(data.has_value());
    CHECK(data->get().payload_kind == ahfl::EnumVariantPayloadKind::Struct);
    REQUIRE(data->get().fields.size() == 2);
    CHECK(data->get().fields[0].name == "code");
    CHECK(data->get().fields[0].type->describe() == "Int");

    const auto snapshot = ahfl::serialize_typed_program_json(result.typed_program);
    const auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
}

TEST_CASE("struct variant pattern supports explicit field pattern and constructor field order") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { label: "ok", code: 7 })AHFL",
        "match ctx.value { Data { label: _, code: captured } => captured, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

TEST_CASE("struct variant constructor may omit defaulted field") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String = "ok" },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7 })AHFL",
        "match ctx.value { Data { code, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK_FALSE(result.has_errors());
}

TEST_CASE("struct variant default field value must match declared type") {
    const auto source = module_preamble() + R"AHFL(
enum Packet {
    Data { code: Int = "bad" },
}
)AHFL";
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
}

TEST_CASE("struct variant pattern missing field reports MISSING_VARIANT_FIELD") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data { code } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MISSING_VARIANT_FIELD"));
}

TEST_CASE("struct variant pattern unexpected field reports UNEXPECTED_VARIANT_FIELD") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data { code, extra, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "UNEXPECTED_VARIANT_FIELD"));
}

TEST_CASE("tuple pattern on struct variant reports INVALID_ENUM_VARIANT_SHAPE") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data(code) => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "INVALID_ENUM_VARIANT_SHAPE"));
}

TEST_CASE("variant shape diagnostic points to variant declaration") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, label: "ok" })AHFL",
        "match ctx.value { Data(code) => code, Empty => 0 }");
    const auto variant_offset = source.find("Data { code");
    REQUIRE(variant_offset != std::string::npos);

    const auto result = typecheck_source(source);
    CHECK(result.has_errors());

    bool saw_shape_diagnostic = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (!entry.code.has_value() ||
            entry.code->find("INVALID_ENUM_VARIANT_SHAPE") == std::string::npos) {
            continue;
        }
        saw_shape_diagnostic = true;
        REQUIRE_FALSE(entry.related.empty());
        CHECK(entry.related.front().message == "variant 'Data' declared here");
        REQUIRE(entry.related.front().range.has_value());
        CHECK(entry.related.front().range->begin_offset == variant_offset);
        REQUIRE(entry.related.front().source_name.has_value());
        CHECK(*entry.related.front().source_name == "module 'adt_match'");
        break;
    }
    CHECK(saw_shape_diagnostic);
}

TEST_CASE("struct pattern on tuple variant reports INVALID_ENUM_VARIANT_SHAPE") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data(Int),
}
)AHFL",
        "Packet",
        "Packet::Empty",
        "match ctx.value { Data { code } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "INVALID_ENUM_VARIANT_SHAPE"));
}

TEST_CASE("tuple pattern on unit variant reports INVALID_ENUM_VARIANT_SHAPE") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data(Int),
}
)AHFL",
        "Packet",
        "Packet::Empty",
        "match ctx.value { Empty(x) => x, Data(code) => code }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "INVALID_ENUM_VARIANT_SHAPE"));
}

TEST_CASE("struct variant constructor missing required field reports diagnostic") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7 })AHFL",
        "match ctx.value { Data { code, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "MISSING_VARIANT_FIELD_IN_CONSTRUCTOR"));
}

TEST_CASE("struct variant constructor duplicate field reports DUPLICATE_VARIANT_FIELD") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int, label: String },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, code: 8, label: "ok" })AHFL",
        "match ctx.value { Data { code, .. } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "DUPLICATE_VARIANT_FIELD"));
    CHECK_FALSE(has_diagnostic_code(result, "DUPLICATE_FIELD"));
}

TEST_CASE("struct variant constructor unexpected field reports UNEXPECTED_VARIANT_FIELD") {
    const auto source = wrap_in_flow(
        R"AHFL(
enum Packet {
    Empty,
    Data { code: Int },
}
)AHFL",
        "Packet",
        R"AHFL(Packet::Data { code: 7, extra: 9 })AHFL",
        "match ctx.value { Data { code } => code, Empty => 0 }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "UNEXPECTED_VARIANT_FIELD"));
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
        // Err/Initial not covered -> MATCH_MISSING_PATTERNS.
        "match ctx.value { Ok(v) => v }");
    const auto result = typecheck_source(source);
    CHECK(result.has_errors());
    const auto *diagnostic =
        find_diagnostic_with_code(result.diagnostics, "MATCH_MISSING_PATTERNS");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message.find("Err") != std::string::npos);
    CHECK(diagnostic->message.find("Initial") != std::string::npos);
    CHECK(related_contains(*diagnostic, "missing variant 'Err' declared here"));
    CHECK(related_contains(*diagnostic, "missing variant 'Initial' declared here"));
}

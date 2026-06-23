#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Full parse -> resolve -> typecheck -> validate pipeline.
// ---------------------------------------------------------------------------
struct PipelineResult {
    ahfl::ParseResult parse;
    ahfl::ResolveResult resolve;
    ahfl::TypeCheckResult typecheck;
    ahfl::ValidationResult validation;
};

[[nodiscard]] PipelineResult run_pipeline(const std::string &source,
                                          std::string_view path = "validate_plumbing.ahfl") {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(std::string{path}, source);
    if (parse_result.has_errors()) {
        for (const auto &d : parse_result.diagnostics.entries()) {
            MESSAGE("parse diagnostic: ", d.message);
        }
    }
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        for (const auto &d : resolve_result.diagnostics.entries()) {
            MESSAGE("resolve diagnostic: ", d.message);
        }
    }
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto typecheck_result = type_checker.check(*parse_result.program, resolve_result);
    if (typecheck_result.has_errors()) {
        for (const auto &d : typecheck_result.diagnostics.entries()) {
            MESSAGE("typecheck diagnostic: ", d.message);
        }
    }
    REQUIRE_FALSE(typecheck_result.has_errors());

    const ahfl::Validator validator;
    auto validation_result =
        validator.validate(*parse_result.program, resolve_result, typecheck_result);
    if (validation_result.has_errors()) {
        for (const auto &d : validation_result.diagnostics.entries()) {
            MESSAGE("validate diagnostic: ", d.message);
        }
    }

    return PipelineResult{
        .parse = std::move(parse_result),
        .resolve = std::move(resolve_result),
        .typecheck = std::move(typecheck_result),
        .validation = std::move(validation_result),
    };
}

// ---------------------------------------------------------------------------
// Fixture preamble: an agent with an Int payload, empty capabilities, a
// one-state flow that just returns. Tests inject their own contract blocks.
// ---------------------------------------------------------------------------
constexpr std::string_view kAgentPreamble = R"AHFL(
struct Request {
    a: Int;
    b: Int;
    c: Int;
}

struct UnitCtx { }
)AHFL";

[[nodiscard]] std::string agent_with_contract(std::string_view agent_name,
                                              std::string_view contract_body) {
    return std::string{kAgentPreamble} + "agent " + std::string{agent_name} + R"AHFL( {
    input: Request;
    context: UnitCtx;
    output: UnitCtx;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for )AHFL" + std::string{agent_name} + R"AHFL( {
    state Done {
        return UnitCtx { };
    }
}

contract for )AHFL" + std::string{agent_name} + " {\n" + std::string{contract_body} + R"AHFL(
}
)AHFL";
}

} // namespace

// ---------------------------------------------------------------------------
// TC1: Positive fixture — exactly 3 concrete decreases + 1 wildcard decreases
// => counters total=4, wildcard=1 (the acceptance condition).
// ---------------------------------------------------------------------------
TEST_CASE("Validation counts 3 concrete decreases and 1 wildcard decreases") {
    // 3 concrete metrics + 1 wildcard = total=4, wildcard=1
    const std::string contract_body = R"AHFL(
    requires: input.a > 0;
    decreases: input.a;
    decreases: input.b;
    ensures: true;
    decreases: input.c;
    decreases: *;
)AHFL";
    const auto source = agent_with_contract("CounterAgent", contract_body);
    const auto result = run_pipeline(source, "tc1.ahfl");
    CHECK_FALSE(result.validation.has_errors());

    REQUIRE_EQ(result.validation.total_decreases_clauses, 4u);
    REQUIRE_EQ(result.validation.wildcard_decreases_clauses, 1u);
}

// ---------------------------------------------------------------------------
// TC2: Absence of decreases => counters 0/0. Reverse-control fixture used
//      by S5b to detect a missed traversal (R-04).
// ---------------------------------------------------------------------------
TEST_CASE("Validation reports zero decreases when no decreases clauses present") {
    const std::string contract_body = R"AHFL(
    requires: input.a > 0;
    ensures: true;
    invariant: always true;
)AHFL";
    const auto source = agent_with_contract("PlainAgent", contract_body);
    const auto result = run_pipeline(source, "tc2.ahfl");
    CHECK_FALSE(result.validation.has_errors());

    REQUIRE_EQ(result.validation.total_decreases_clauses, 0u);
    REQUIRE_EQ(result.validation.wildcard_decreases_clauses, 0u);
}

// ---------------------------------------------------------------------------
// TC3: ValidationResult exposes the new fields with zero defaults.
// ---------------------------------------------------------------------------
TEST_CASE("ValidationReport exposes new decreases counter fields") {
    // Default-constructed result must have zeroed counters — required by the
    // "missed traversal counts as 0" property in R-04.
    const ahfl::ValidationResult defaulted{};
    CHECK_EQ(defaulted.total_decreases_clauses, 0u);
    CHECK_EQ(defaulted.wildcard_decreases_clauses, 0u);

    // A minimal wildcard-only contract to confirm fields propagate through
    // Validator::validate (i.e. not stripped by std::move).
    const auto source = agent_with_contract("WildcardOnly", "decreases: *;");
    const auto result = run_pipeline(source, "tc3.ahfl");
    CHECK_FALSE(result.validation.has_errors());
    CHECK_EQ(result.validation.total_decreases_clauses, 1u);
    CHECK_EQ(result.validation.wildcard_decreases_clauses, 1u);
}

// ---------------------------------------------------------------------------
// TC4: Mixed concrete and wildcard across multiple contracts aggregates.
// ---------------------------------------------------------------------------
TEST_CASE("Validation aggregates decreases counters across multiple contracts") {
    const std::string source = std::string{kAgentPreamble} + R"AHFL(
agent Alpha {
    input: Request;
    context: UnitCtx;
    output: UnitCtx;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
flow for Alpha { state Done { return UnitCtx { }; } }
contract for Alpha {
    decreases: input.a;
    decreases: input.b;
    decreases: *;
}

agent Beta {
    input: Request;
    context: UnitCtx;
    output: UnitCtx;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
flow for Beta { state Done { return UnitCtx { }; } }
contract for Beta {
    decreases: input.c;
    decreases: input.a;
    decreases: *;
    decreases: *;
}
)AHFL";

    const auto result = run_pipeline(source, "tc4.ahfl");
    CHECK_FALSE(result.validation.has_errors());
    // Alpha: 2 concrete + 1 wildcard; Beta: 2 concrete + 2 wildcards.
    CHECK_EQ(result.validation.total_decreases_clauses, 7u);
    CHECK_EQ(result.validation.wildcard_decreases_clauses, 3u);
}

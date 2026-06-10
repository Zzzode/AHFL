#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] ahfl::SourceRange range_of(std::string_view source, std::string_view needle) {
    const auto offset = source.find(needle);
    REQUIRE(offset != std::string_view::npos);
    return ahfl::SourceRange{
        .begin_offset = offset,
        .end_offset = offset + needle.size(),
    };
}

} // namespace

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
        for (const auto &related : entry.related) {
            if (related.message.find(needle) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::size_t diagnostic_count_with_code(const ahfl::DiagnosticBag &diagnostics,
                                                     std::string_view code) {
    std::size_t count = 0;
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] const ahfl::Diagnostic *diagnostic_with_code(const ahfl::DiagnosticBag &diagnostics,
                                                           std::string_view code) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            return &entry;
        }
    }
    return nullptr;
}

TEST_CASE("Expression effects distinguish predicate and capability calls") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(request: Request) -> Response;

agent EffectAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Do];
    transition Init -> Done;
}

contract for EffectAgent {
    requires: Ready(input.value);
}

flow for EffectAgent {
    state Init {
        let reply = Do(input);
        ctx.value = reply.value;
        goto Done;
    }

    state Done {
        return Response {
            value: ctx.value,
        };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("effect_test.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto *predicate_effect = type_result.typed_program.find_expr_by_range(
        range_of(source, "Ready(input.value)"), std::nullopt);
    REQUIRE(predicate_effect != nullptr);
    CHECK(predicate_effect->effect == ahfl::ExprEffect::PredicateCall);
    CHECK(predicate_effect->is_pure);

    const auto *capability_effect = type_result.typed_program.find_expr_by_range(
        range_of(source, "Do(input)"), std::nullopt);
    REQUIRE(capability_effect != nullptr);
    CHECK(capability_effect->effect == ahfl::ExprEffect::CapabilityCall);
    CHECK_FALSE(capability_effect->is_pure);
}

TEST_CASE("Optional narrowing supports symmetric member facts and else complements") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = none;
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if (none != ctx.token) {
            return Response { value: ctx.token };
        } else {
            if (ctx.token == none) {
                return Response { value: input.fallback };
            } else {
                return Response { value: ctx.token };
            }
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("optional_narrowing.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    CHECK_FALSE(type_result.has_errors());
}

TEST_CASE("Optional narrowing is invalidated by assignment") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = some("seed");
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if (ctx.token != none) {
            ctx.token = none;
            return Response { value: ctx.token };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("optional_invalidation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    CHECK(type_result.has_errors());
}

TEST_CASE("Optional narrowing explanations are emitted only in debug mode") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = none;
}

struct Response {
    value: String;
}

agent NarrowDebugAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowDebugAgent {
    state Done {
        if (ctx.token != none) {
            return Response { value: ctx.token };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("optional_narrowing_debug.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto default_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(default_result.has_errors());
    CHECK(default_result.diagnostics.entries().empty());

    const auto debug_result = type_checker.check(
        *parse_result.program,
        resolve_result,
        ahfl::TypeCheckOptions{.explain_narrowing = true});
    REQUIRE_FALSE(debug_result.has_errors());
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition 'ctx.token != none' narrows 'ctx.token' to non-none on then branch"));
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition 'ctx.token != none' narrows 'ctx.token' to none on else branch"));
}

TEST_CASE("Optional narrowing explanations describe unsupported disjunctive conditions") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = none;
}

struct Response {
    value: String;
}

agent NarrowDebugUnsupportedAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowDebugUnsupportedAgent {
    state Done {
        if (ctx.token != none || input.fallback != "") {
            return Response { value: input.fallback };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("optional_narrowing_debug_unsupported.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto debug_result = type_checker.check(
        *parse_result.program,
        resolve_result,
        ahfl::TypeCheckOptions{.explain_narrowing = true});
    REQUIRE_FALSE(debug_result.has_errors());
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition 'ctx.token != none || input.fallback != \"\"' did not produce Optional narrowing facts because disjunctive conditions are not represented"));
}

TEST_CASE("Type diagnostics suggest nearby struct fields") {
    const std::string source = R"AHFL(
struct Request {
    name: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent SuggestAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SuggestAgent {
    state Done {
        return Response { value: input.naem };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("field_suggestion.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics, "did you mean 'name'?"));
}

TEST_CASE("Type diagnostics suggest nearby enum variants") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

enum Status {
    Pending,
    Approved,
    Rejected,
}

struct Response {
    status: Status;
}

agent SuggestEnumAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SuggestEnumAgent {
    state Done {
        return Response { status: Status::Aproved };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("enum_variant_suggestion.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics, "did you mean 'Approved'?"));
}

TEST_CASE("Type diagnostics describe struct field expectation origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent ExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ExpectationAgent {
    state Done {
        return Response { value: 1 };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("struct_field_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from struct field 'value' declared here"));
}

TEST_CASE("Type diagnostics preserve struct field expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    values: List<String>;
}

agent NestedExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NestedExpectationAgent {
    state Done {
        return Response { values: [1] };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("struct_field_list_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from struct field 'values' declared here"));
}

TEST_CASE("Type diagnostics preserve struct field expectation through grouped list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    values: List<String>;
}

agent GroupedExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for GroupedExpectationAgent {
    state Done {
        return Response { values: ([1]) };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("struct_field_grouped_list_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from struct field 'values' declared here"));
}

TEST_CASE("Type diagnostics describe capability parameter expectation origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(value: String) -> Response;

agent ParameterExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Do];
}

flow for ParameterExpectationAgent {
    state Done {
        return Do(1);
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("capability_parameter_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from parameter 'value' declared here"));
}

TEST_CASE("Type diagnostics preserve flow return expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

agent ReturnExpectationAgent {
    input: Request;
    context: Context;
    output: List<String>;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ReturnExpectationAgent {
    state Done {
        return [1];
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("flow_return_list_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from flow return declared here"));
}

TEST_CASE("Type diagnostics preserve assignment target expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    values: List<String> = [];
}

struct Response {
    value: String;
}

agent AssignmentExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for AssignmentExpectationAgent {
    state Done {
        ctx.values = [1];
        return Response { value: input.fallback };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("assignment_target_list_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'String' from assignment target 'ctx.values' declared here"));
}

TEST_CASE("Type diagnostics describe actual expression origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent ActualOriginAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ActualOriginAgent {
    state Done {
        return Response { value: 1 };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("actual_expression_origin.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual expression has type 'Int' here"));
}

TEST_CASE("Type diagnostics preserve assignment expectation through some literals") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = none;
}

struct Response {
    value: String;
}

agent SomeExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SomeExpectationAgent {
    state Done {
        ctx.token = some(1);
        return Response { value: input.fallback };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("some_assignment_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH") == 1);
    const auto *diagnostic = diagnostic_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() == 2);
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'String' from assignment target 'ctx.token' declared here"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual expression has type 'Int' here"));
}

TEST_CASE("Type diagnostics describe actual expression origins for schema boundaries") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

struct WiderResponse {
    value: String;
    extra: String;
}

agent SchemaActualOriginAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SchemaActualOriginAgent {
    state Done {
        return WiderResponse { value: input.value, extra: "extra" };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("schema_actual_expression_origin.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.EXACT_SCHEMA_MISMATCH") == 1);
    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.EXACT_SCHEMA_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() == 3);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "exact schema match"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual expression has type 'WiderResponse' here"));
}

TEST_CASE("Type checker records typed HIR expressions alongside legacy expression types") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(value: String) -> Response;

agent HirAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Do];
}

flow for HirAgent {
    state Done {
        let ok = true;
        let token: Optional<String> = none;
        let reply = Response { value: input.value };
        let grouped = Response { value: (input.value) };
        let ready = Ready((reply).value);
        return Do(reply.value);
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto response_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "Response");
    REQUIRE(response_symbol.has_value());
    const auto do_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Capabilities, "Do");
    REQUIRE(do_symbol.has_value());
    const auto ready_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Predicates, "Ready");
    REQUIRE(ready_symbol.has_value());

    REQUIRE_FALSE(type_result.typed_program.expressions.empty());
    CHECK_FALSE(type_result.typed_program.declarations.empty());

    const auto &expression = type_result.typed_program.expressions.front();
    const auto *typed_expr =
        type_result.typed_program.find_expr(expression.node_id, expression.source_id);
    REQUIRE(typed_expr != nullptr);
    CHECK(typed_expr->type == expression.type);
    CHECK(typed_expr->effect == expression.effect);
    CHECK(typed_expr->is_pure == expression.is_pure);

    const auto struct_literal = std::find_if(
        type_result.typed_program.expressions.begin(),
        type_result.typed_program.expressions.end(),
        [](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral;
        });
    REQUIRE(struct_literal != type_result.typed_program.expressions.end());
    REQUIRE(struct_literal->resolved_symbol.has_value());
    CHECK(*struct_literal->resolved_symbol == response_symbol->get().id);
    CHECK(struct_literal->semantic_name == "Response");
    REQUIRE(struct_literal->children.size() == 1);
    CHECK(struct_literal->children.front().role == ahfl::TypedExprChildRole::StructFieldValue);
    CHECK(struct_literal->children.front().name == "value");
    const auto *struct_child = ahfl::resolve_child(
        type_result.typed_program, struct_literal->children.front());
    REQUIRE(struct_child != nullptr);
    CHECK(struct_child->kind == ahfl::ast::ExprSyntaxKind::Path);
    CHECK(struct_child->semantic_name == "input.value");
    CHECK(struct_child->path_root == "input");
    CHECK(struct_child->member_path == std::vector<std::string>{"value"});
    CHECK(struct_child->type->holds<ahfl::types::StringT>());

    const auto &tp = type_result.typed_program;
    const auto grouped_struct_literal = std::find_if(
        tp.expressions.begin(), tp.expressions.end(),
        [&tp](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral &&
                   expr.children.size() == 1 &&
                   ahfl::resolve_child(tp, expr.children.front()) != nullptr &&
                   ahfl::resolve_child(tp, expr.children.front())->kind ==
                       ahfl::ast::ExprSyntaxKind::Group;
        });
    REQUIRE(grouped_struct_literal != tp.expressions.end());
    const auto *grouped_child =
        ahfl::resolve_child(tp, grouped_struct_literal->children.front());
    CHECK(grouped_child->semantic_name == "input.value");
    CHECK(grouped_child->path_root == "input");
    CHECK(grouped_child->member_path == std::vector<std::string>{"value"});

    const auto call = std::find_if(tp.expressions.begin(), tp.expressions.end(),
                                   [](const ahfl::TypedExpr &expr) {
                                       return expr.kind == ahfl::ast::ExprSyntaxKind::Call &&
                                              expr.semantic_name == "Do";
                                   });
    REQUIRE(call != tp.expressions.end());
    REQUIRE(call->resolved_symbol.has_value());
    CHECK(*call->resolved_symbol == do_symbol->get().id);
    CHECK(call->call_target_kind == ahfl::TypedCallTargetKind::Capability);
    CHECK(call->semantic_name == "Do");
    REQUIRE(call->children.size() == 1);
    CHECK(call->children.front().role == ahfl::TypedExprChildRole::Argument);
    CHECK(call->children.front().name == "value");
    const auto *call_child = ahfl::resolve_child(tp, call->children.front());
    REQUIRE(call_child != nullptr);
    CHECK(call_child->semantic_name == "reply.value");
    CHECK(call_child->path_root == "reply");
    CHECK(call_child->member_path == std::vector<std::string>{"value"});

    const auto predicate_call = std::find_if(
        tp.expressions.begin(), tp.expressions.end(),
        [](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::Call &&
                   expr.semantic_name == "Ready";
        });
    REQUIRE(predicate_call != tp.expressions.end());
    REQUIRE(predicate_call->resolved_symbol.has_value());
    CHECK(*predicate_call->resolved_symbol == ready_symbol->get().id);
    CHECK(predicate_call->call_target_kind == ahfl::TypedCallTargetKind::Predicate);
    REQUIRE(predicate_call->children.size() == 1);
    CHECK(predicate_call->children.front().role == ahfl::TypedExprChildRole::Argument);
    CHECK(predicate_call->children.front().name == "value");
    const auto *pred_child = ahfl::resolve_child(tp, predicate_call->children.front());
    REQUIRE(pred_child != nullptr);
    CHECK(pred_child->kind == ahfl::ast::ExprSyntaxKind::MemberAccess);
    CHECK(pred_child->semantic_name == "reply.value");
    CHECK(pred_child->path_root == "reply");
    CHECK(pred_child->member_path == std::vector<std::string>{"value"});

    const auto bool_literal = std::find_if(type_result.typed_program.expressions.begin(),
                                           type_result.typed_program.expressions.end(),
                                           [](const ahfl::TypedExpr &expr) {
                                               return expr.kind == ahfl::ast::ExprSyntaxKind::BoolLiteral;
                                           });
    REQUIRE(bool_literal != type_result.typed_program.expressions.end());
    CHECK(bool_literal->semantic_name == "true");

    const auto none_literal = std::find_if(type_result.typed_program.expressions.begin(),
                                           type_result.typed_program.expressions.end(),
                                           [](const ahfl::TypedExpr &expr) {
                                               return expr.kind == ahfl::ast::ExprSyntaxKind::NoneLiteral;
                                           });
    REQUIRE(none_literal != type_result.typed_program.expressions.end());
    CHECK(none_literal->semantic_name == "none");
}

TEST_CASE("Type checker can trace relation checks through TypeRelationContext") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent RelationTraceAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for RelationTraceAgent {
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("type_relation_trace.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(
        *parse_result.program,
        resolve_result,
        ahfl::TypeCheckOptions{.trace_type_relations = true});
    REQUIRE_FALSE(type_result.has_errors());

    bool saw_assignable = false;
    bool saw_exact_schema = false;
    for (const auto &step : type_result.relation_trace.steps) {
        saw_assignable = saw_assignable || step.kind == ahfl::TypeRelationKind::Assignable;
        saw_exact_schema = saw_exact_schema || step.kind == ahfl::TypeRelationKind::ExactSchema;
    }
    CHECK(saw_assignable);
    CHECK(saw_exact_schema);
}

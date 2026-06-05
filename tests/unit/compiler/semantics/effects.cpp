#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <string>
#include <string_view>

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

    const auto predicate_effect =
        type_result.find_expression_type(range_of(source, "Ready(input.value)"));
    REQUIRE(predicate_effect.has_value());
    CHECK(predicate_effect->get().effect == ahfl::ExprEffect::PredicateCall);
    CHECK(predicate_effect->get().is_pure);

    const auto capability_effect = type_result.find_expression_type(range_of(source, "Do(input)"));
    REQUIRE(capability_effect.has_value());
    CHECK(capability_effect->get().effect == ahfl::ExprEffect::CapabilityCall);
    CHECK_FALSE(capability_effect->get().is_pure);
}

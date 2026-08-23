#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <string>
#include <string_view>

namespace {

// Full parse -> resolve -> typecheck pipeline. Surfaces the first diagnostic
// via MESSAGE so an unexpected failure gives an actionable cause.
[[nodiscard]] ahfl::TypeCheckResult typecheck_source(std::string_view filename,
                                                     const std::string &source) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text(std::string(filename), source);
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

// True when at least one diagnostic carries the given error code substring.
[[nodiscard]] bool has_diagnostic_code(const ahfl::TypeCheckResult &result,
                                       std::string_view code_substring) {
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() &&
            entry.code->find(code_substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// True when at least one diagnostic carries TRAIT_BOUND_NOT_SATISFIED.
[[nodiscard]] bool has_bound_not_satisfied(const ahfl::TypeCheckResult &result) {
    return has_diagnostic_code(result, "TRAIT_BOUND_NOT_SATISFIED");
}

} // namespace

// ===========================================================================
// RFC 0013 P2-S1 exit-criteria tests for the cross-chain generic inference
// engine (Steps 2-10). Each test maps to a specific rule (R1-R7.1) or exit
// criterion from the revision spec.
// ===========================================================================

// ---------------------------------------------------------------------------
// Exit criterion 4 / R2: a generic fn called with no context that constrains
// its type parameter emits TYPE_PARAMETER_AMBIGUOUS, does not crash, and does
// not record a call site. The `MyNone` argument adopts the expected param
// type `MyOpt<T>` (a TypeVar of the callee's own scope), so the subst entry
// is non-null but still unbound per the R2 concreteness test.
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1: unconstrained type parameter emits TYPE_PARAMETER_AMBIGUOUS") {
    const std::string source = R"AHFL(
module p2s1_ambiguous;

enum MyOpt<T> {
    MySome(T),
    MyNone,
}

fn is_none<T>(x: MyOpt<T>) -> Bool effect Pure decreases 0 {
    return true;
}

fn run() -> Bool effect Pure decreases 0 {
    return is_none(MyOpt::MyNone);
}
)AHFL";

    const auto result = typecheck_source("p2s1_ambiguous.ahfl", source);
    REQUIRE(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_PARAMETER_AMBIGUOUS"));
}

// ---------------------------------------------------------------------------
// R1: explicit type arguments are honored even when the surrounding expected
// type disagrees. `identity<Int>(42)` has T=Int from the explicit arg; the
// expected return type String must not clobber it. The surfaces as a
// TYPE_MISMATCH (Int vs String), proving the explicit arg won.
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R1: explicit type arg wins over expected return type") {
    const std::string source = R"AHFL(
module p2s1_r1_explicit;

fn identity<T>(x: T) -> T effect Pure decreases 0 {
    return x;
}

fn run() -> Int effect Pure decreases 0 {
    let s: String = identity<Int>(42);
    return 0;
}
)AHFL";

    const auto result = typecheck_source("p2s1_r1_explicit.ahfl", source);
    REQUIRE(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
    // The ambiguity diagnostic must NOT fire — T was pinned by the explicit arg.
    CHECK_FALSE(has_diagnostic_code(result, "TYPE_PARAMETER_AMBIGUOUS"));
}

// ---------------------------------------------------------------------------
// R5: lambda body assignability — a lambda whose inferred body type is a
// TypeVar is checked against a concrete expected return type. `\x -> x` has
// body type T; the expected return type String (from the let annotation via
// prefill) is concrete, so the assignability check fires TYPE_MISMATCH.
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R5: lambda body TypeVar vs concrete expected return is TYPE_MISMATCH") {
    const std::string source = R"AHFL(
module p2s1_r5_lambda;

fn apply<T, U>(f: Fn(T) -> U, x: T) -> U effect Pure decreases 0 {
    return f(x);
}

fn run() -> Int effect Pure decreases 0 {
    let s: String = apply(\x -> x, 42);
    return 0;
}
)AHFL";

    const auto result = typecheck_source("p2s1_r5_lambda.ahfl", source);
    REQUIRE(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
}

// ---------------------------------------------------------------------------
// R6: where-check TypeVar guard — a generic fn body calling a where-bounded
// callee with the caller's own TypeVar must NOT emit
// TRAIT_BOUND_NOT_SATISFIED. The subst entry resolves to a TypeVarT, so the
// bound check is skipped (the caller's where-clause is responsible).
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R6: where-bounded call with caller TypeVar skips bound check") {
    const std::string source = R"AHFL(
module p2s1_r6_where;

struct Box {
    value: Int;
}

trait Show {
    fn render(self: Box) -> Int;
}

impl Show for Box {
    fn render(self: Box) -> Int {
        return self.value;
    }
}

fn display<T>(x: T) -> T effect Pure decreases 0 where T: Show {
    return x;
}

fn outer<T>(x: T) -> T effect Pure decreases 0 where T: Show {
    return display(x);
}

fn run() -> Box effect Pure decreases 0 {
    let b = Box { value: 42 };
    return outer(b);
}
)AHFL";

    const auto result = typecheck_source("p2s1_r6_where.ahfl", source);
    CHECK_FALSE(result.has_errors());
    CHECK_FALSE(has_bound_not_satisfied(result));
}

// ---------------------------------------------------------------------------
// R6 positive control: a where-bounded call with a concrete type that
// implements the bound still passes (the R6 guard only skips TypeVar
// subjects, not concrete ones).
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R6: concrete where-bound call still checks and passes") {
    const std::string source = R"AHFL(
module p2s1_r6_positive;

struct Box {
    value: Int;
}

trait Show {
    fn render(self: Box) -> Int;
}

impl Show for Box {
    fn render(self: Box) -> Int {
        return self.value;
    }
}

fn display<T>(x: T) -> T effect Pure decreases 0 where T: Show {
    return x;
}

fn run() -> Box effect Pure decreases 0 {
    let b = Box { value: 42 };
    return display(b);
}
)AHFL";

    const auto result = typecheck_source("p2s1_r6_positive.ahfl", source);
    CHECK_FALSE(result.has_errors());
    CHECK_FALSE(has_bound_not_satisfied(result));
}

// ---------------------------------------------------------------------------
// R3: incremental left-fold — the first argument binds a type parameter that
// the second argument (a lambda) depends on. `map(xs, \x -> x + 1)` must
// infer T=Int from xs so the lambda param x gets Int and `x + 1` typechecks.
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R3: left-fold binds T from first arg so lambda param typechecks") {
    const std::string source = R"AHFL(
module p2s1_r3_leftfold;

enum MyOpt<T> {
    MySome(T),
    MyNone,
}

fn map<T, U>(x: MyOpt<T>, f: Fn(T) -> U) -> MyOpt<U> effect Pure decreases 0 {
    return MyOpt::MyNone;
}

fn run() -> Bool effect Pure decreases 0 {
    let xs = MyOpt::MySome(5);
    let result = map(xs, \x -> x + 1);
    return true;
}
)AHFL";

    const auto result = typecheck_source("p2s1_r3_leftfold.ahfl", source);
    CHECK_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// R4: struct-variant constructor with lambda inference — a struct variant
// `MySome { value: x + 1 }` used as a lambda body must typecheck with no
// ambiguity diagnostic. The struct-variant path uses unify-first (R4) so the
// field expression drives inference.
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R4: struct variant in lambda body typechecks without ambiguity") {
    const std::string source = R"AHFL(
module p2s1_r4_struct;

enum MyOpt<T> {
    MySome { value: T },
    MyNone,
}

fn and_then<T, U>(x: MyOpt<T>, f: Fn(T) -> MyOpt<U>) -> MyOpt<U> effect Pure decreases 0 {
    return MyOpt::MyNone;
}

fn run() -> Bool effect Pure decreases 0 {
    let xs = MyOpt::MySome { value: 1 };
    let result = and_then(xs, \x -> MyOpt::MySome { value: x + 1 });
    return true;
}
)AHFL";

    const auto result = typecheck_source("p2s1_r4_struct.ahfl", source);
    CHECK_FALSE(result.has_errors());
    CHECK_FALSE(has_diagnostic_code(result, "TYPE_PARAMETER_AMBIGUOUS"));
}

// ---------------------------------------------------------------------------
// R2 (method-call emission site): the second TYPE_PARAMETER_AMBIGUOUS emission
// site lives in the method-call path (check_method_call), distinct from the
// free-fn call site exercised by the first test above (`is_none(...)` routes
// through check_fn_call). Here a generic *method* declares a method-level type
// parameter `U` that appears in neither the receiver nor any argument, so no
// inference source can pin it; the method-call ambiguity branch fires.
//
// Impl-level type params are always inferred from the receiver and are skipped
// by the branch, so `U` must be method-level (`fn pick<U>`) to reach the
// diagnostic. This confirms both emission sites carry the same code + message
// shape (the message names the method, not the free fn).
// ---------------------------------------------------------------------------
TEST_CASE("P2-S1 R2: unconstrained method type parameter emits TYPE_PARAMETER_AMBIGUOUS") {
    const std::string source = R"AHFL(
module p2s1_method_ambiguous;

struct Wrap {
    value: Int;
}

impl Wrap {
    fn pick<U>(self: Wrap) -> Int effect Pure decreases 0 {
        return self.value;
    }
}

fn run(w: Wrap) -> Int effect Pure decreases 0 {
    return w.pick();
}
)AHFL";

    const auto result = typecheck_source("p2s1_method_ambiguous.ahfl", source);
    REQUIRE(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_PARAMETER_AMBIGUOUS"));
    // The method-call site formats the message with the method name `pick`,
    // distinguishing it from the free-fn site (which names the callee fn).
    bool named_method = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == "typecheck.TYPE_PARAMETER_AMBIGUOUS" &&
            entry.message.find("type parameter 'U' of 'pick'") != std::string::npos) {
            named_method = true;
        }
    }
    CHECK(named_method);
}

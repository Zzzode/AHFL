#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/monomorphization.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace {

// ---------------------------------------------------------------------------
// P2 (RFC corelib-rfc.zh.md §6) fn + user generics + first-class closures.
//
// The P2 acceptance surface (RFC §6 "验收"):
//   * `fn id<T>(x: T) -> T { x }` — top-level generic fn with a body and an
//     effect clause.
//   * Closures as higher-order function arguments are usable and
//     monomorphized.
//
// P2a (grammar / AST), P2b (typecheck records fn call sites, accepts lambda
// expressions structurally) and P2c (monomorphization pass builds the
// instantiation set) are landed. These tests exercise the end-to-end
// parse -> resolve -> typecheck pipeline plus the monomorphization pass over
// the recorded fn call sites, asserting only what P2a-c commit to today:
//
//   - a generic fn with `effect Pure` parses, resolves and typechecks clean;
//   - the typed program carries a Function symbol + a TypedDecl for the fn;
//   - a call with explicit `<Type>` args is recorded as an fn_call_site with
//     the supplied type arguments;
//   - the monomorphization pass produces a distinct typed instance per unique
//     (decl, type_args) pair and dedups repeated instantiations.
//
// They deliberately avoid over-constraining fn-body typechecking semantics
// still in flight (see monomorphization.hpp P2c scope note) — the body of a
// generic fn is type-checked structurally today, so the assertions lean on
// declaration indexing, call-site recording and the monomorphization result
// shape, which is the surface P2d must validate.
// ---------------------------------------------------------------------------

// Full parse -> resolve -> typecheck pipeline. Surfaces the first diagnostic
// via MESSAGE so an unexpected failure gives an actionable cause rather than
// a silent boolean.
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

// Look up a Function symbol in the typed program's resolver snapshot. Returns
// nullptr when the fn name is not registered.
[[nodiscard]] const ahfl::Symbol *
find_function_symbol(const ahfl::TypeCheckResult &result, std::string_view name) {
    const auto found = result.typed_program.find_local_symbol(
        ahfl::SymbolNamespace::Functions, name);
    return found.has_value() ? &found.value().get() : nullptr;
}

// True when a TypedDecl of kind FnDecl is present for the given fn name.
[[nodiscard]] bool has_fn_typed_decl(const ahfl::TypeCheckResult &result,
                                     std::string_view name) {
    const auto *symbol = find_function_symbol(result, name);
    if (symbol == nullptr) {
        return false;
    }
    return std::any_of(
        result.typed_program.declarations.begin(),
        result.typed_program.declarations.end(),
        [target = symbol->id, kind = ahfl::ast::NodeKind::FnDecl](const ahfl::TypedDecl &decl) {
            return decl.kind == kind && decl.symbol == target;
        });
}

} // namespace

// ---------------------------------------------------------------------------
// TC1: RFC §6 acceptance baseline — `fn id<T>(x: T) -> T { x }` with a Pure
// effect clause typechecks cleanly and is indexed as a Function declaration.
// ---------------------------------------------------------------------------
TEST_CASE("Generic identity fn with Pure effect clause typechecks") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure {
    return x;
}
)AHFL";

    const auto result = typecheck_source("fn_identity.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "id"));
    const auto *symbol = find_function_symbol(result, "id");
    REQUIRE(symbol != nullptr);
    CHECK(symbol->kind == ahfl::SymbolKind::Function);
}

// ---------------------------------------------------------------------------
// TC2: A second top-level fn (non-generic) co-exists with the generic one and
// both resolve to distinct Function symbols. Guards the "P2 is purely
// additive" backward-compat constraint from RFC §6.
// ---------------------------------------------------------------------------
TEST_CASE("Multiple fn declarations resolve to distinct Function symbols") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure {
    return x;
}

fn add_one(n: Int) -> Int effect Pure {
    return n + 1;
}
)AHFL";

    const auto result = typecheck_source("fn_multiple.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "id"));
    REQUIRE(has_fn_typed_decl(result, "add_one"));

    const auto *id_symbol = find_function_symbol(result, "id");
    const auto *add_symbol = find_function_symbol(result, "add_one");
    REQUIRE(id_symbol != nullptr);
    REQUIRE(add_symbol != nullptr);
    CHECK(id_symbol->id != add_symbol->id);
}

// ---------------------------------------------------------------------------
// TC3: The three RFC §2 effect clause shapes (Pure / Nondet / capability
// list) all parse and typecheck at the declaration level.
// ---------------------------------------------------------------------------
TEST_CASE("Each effect clause shape typechecks at the fn declaration") {
    const std::string source = R"AHFL(
capability ChargeCard(amount: Int) -> Bool;

fn pure_fn(n: Int) -> Int effect Pure {
    return n;
}

fn nondet_fn(n: Int) -> Int effect Nondet {
    return n;
}

fn capability_fn(amount: Int) -> Bool effect ChargeCard {
    return true;
}
)AHFL";

    const auto result = typecheck_source("fn_effect_clauses.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "pure_fn"));
    REQUIRE(has_fn_typed_decl(result, "nondet_fn"));
    REQUIRE(has_fn_typed_decl(result, "capability_fn"));
}

// ---------------------------------------------------------------------------
// TC4: A lambda expression parses and typechecks inside a flow body. This is
// the RFC §3.2.3 first-class closure surface — the lambda is accepted as a
// value (the assertion is on parse/typecheck cleanliness, not on the closure
// body's inferred type which is still structural per P2b scope).
// ---------------------------------------------------------------------------
TEST_CASE("Lambda expression typechecks inside a flow body") {
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

agent ClosureAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ClosureAgent {
    state Done {
        let f = \x -> x;
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = typecheck_source("fn_lambda_expr.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC5: A closure may take multiple parameters with optional type annotations,
// mirroring the RFC §6 grammar forms `\ p -> e`, `\ (p, ...) -> e` and
// `\ -> e`.
// ---------------------------------------------------------------------------
TEST_CASE("Lambda multi-param and zero-param shapes typecheck") {
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

agent ClosureShapesAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ClosureShapesAgent {
    state Done {
        let single = \x -> x;
        let pair = \(a, b) -> a;
        let typed = \(a: Int, b: Int) -> a;
        let thunk = \ -> 1;
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = typecheck_source("fn_lambda_shapes.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC6: Monomorphization pass (P2c) over a recorded generic call site. Two
// call sites with the *same* explicit type argument collapse to a single
// instance; two call sites with *different* type arguments produce two
// distinct instances.
//
// The generic fn here is non-generic from the typechecker's body-viewpoint
// (P2b typechecks fn bodies structurally), but the call sites carry explicit
// `<Type>` arguments that the pass deduplicates.
// ---------------------------------------------------------------------------
TEST_CASE("Monomorphization dedups identical generic instantiations") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure {
    return x;
}

const A: Int = id<Int>(1);
const B: Int = id<Int>(2);
const C: Bool = id<Bool>(true);
)AHFL";

    const auto result = typecheck_source("fn_mono_dedup.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    const auto mono = ahfl::run_monomorphization(result.typed_program);
    // Three call sites, two distinct type-arg keys -> at least two Created
    // instances. id<Int> appears twice but should share one instance.
    REQUIRE(mono.instances.size() >= 2);

    const auto id_int_instances = std::count_if(
        mono.instances.begin(), mono.instances.end(),
        [](const ahfl::MonomorphizationInstance &inst) {
            return inst.instance_name.find("id<Int>") != std::string::npos;
        });
    CHECK(id_int_instances == 1);

    const auto id_bool_instances = std::count_if(
        mono.instances.begin(), mono.instances.end(),
        [](const ahfl::MonomorphizationInstance &inst) {
            return inst.instance_name.find("id<Bool>") != std::string::npos;
        });
    CHECK(id_bool_instances == 1);

    // No diagnostic from the pass: the three sites are well under the 32
    // instance user budget.
    CHECK_FALSE(mono.budget_exceeded());
    CHECK(mono.user_instance_count <= ahfl::kMonomorphizationUserBudget);
}

// ---------------------------------------------------------------------------
// TC7: Monomorphization over a program with no generic call sites yields an
// empty result (no spurious instances, no budget consumption).
// ---------------------------------------------------------------------------
TEST_CASE("Monomorphization over non-generic program is a no-op") {
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

agent PlainAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for PlainAgent {
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = typecheck_source("fn_mono_noop.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    const auto mono = ahfl::run_monomorphization(result.typed_program);
    CHECK(mono.instances.empty());
    CHECK(mono.total_instance_count() == 0);
    CHECK_FALSE(mono.budget_exceeded());
}

// ---------------------------------------------------------------------------
// TC8: Backward compatibility — a program with no P2 constructs at all still
// typechecks clean (guards the RFC §6 "P2 纯新增, 不破 955" constraint).
// ---------------------------------------------------------------------------
TEST_CASE("Legacy program without fn/generics/closures still typechecks") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "seed";
}

struct Response {
    value: String;
}

agent LegacyAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for LegacyAgent {
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = typecheck_source("fn_backward_compat.ahfl", source);
    CHECK_FALSE(result.has_errors());
}

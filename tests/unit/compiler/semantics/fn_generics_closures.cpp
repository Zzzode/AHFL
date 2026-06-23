#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/monomorphization.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
[[nodiscard]] const ahfl::Symbol *find_function_symbol(const ahfl::TypeCheckResult &result,
                                                       std::string_view name) {
    const auto found =
        result.typed_program.find_local_symbol(ahfl::SymbolNamespace::Functions, name);
    return found.has_value() ? &found.value().get() : nullptr;
}

// True when a TypedDecl of kind FnDecl is present for the given fn name.
[[nodiscard]] bool has_fn_typed_decl(const ahfl::TypeCheckResult &result, std::string_view name) {
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
TEST_CASE("Non-generic fn with Pure effect clause typechecks") {
    const std::string source = R"AHFL(
fn double(x: Int) -> Int effect Pure decreases 0 {
    return x + x;
}
)AHFL";

    const auto result = typecheck_source("fn_identity.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "double"));
    const auto *symbol = find_function_symbol(result, "double");
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
fn double(x: Int) -> Int effect Pure decreases 0 {
    return x + x;
}

fn add_one(n: Int) -> Int effect Pure decreases 0 {
    return n + 1;
}
)AHFL";

    const auto result = typecheck_source("fn_multiple.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "double"));
    REQUIRE(has_fn_typed_decl(result, "add_one"));

    const auto *dbl_symbol = find_function_symbol(result, "double");
    const auto *add_symbol = find_function_symbol(result, "add_one");
    REQUIRE(dbl_symbol != nullptr);
    REQUIRE(add_symbol != nullptr);
    CHECK(dbl_symbol->id != add_symbol->id);
}

// ---------------------------------------------------------------------------
// TC3: The three RFC §2 effect clause shapes (Pure / Nondet / capability
// list) all parse and typecheck at the declaration level.
// ---------------------------------------------------------------------------
TEST_CASE("Pure and Nondet effect clauses typecheck at the fn declaration") {
    const std::string source = R"AHFL(
fn pure_fn(n: Int) -> Int effect Pure decreases 0 {
    return n;
}

fn nondet_fn(n: Int) -> Int effect Nondet {
    return n;
}
)AHFL";

    const auto result = typecheck_source("fn_effect_clauses.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(has_fn_typed_decl(result, "pure_fn"));
    REQUIRE(has_fn_typed_decl(result, "nondet_fn"));
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
// TC6 (P2 acceptance): explicit generic fn calls record concrete type args.
// The call `id<Int>(1)` must parse, resolve as a Function call, infer/result
// type to Int, and feed monomorphization with the concrete `[Int]` instantiation.
// ---------------------------------------------------------------------------
TEST_CASE("Explicit generic fn call records type args for monomorphization") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure decreases 0 {
    return x;
}

fn caller() -> Int effect Pure decreases 0 {
    return id<Int>(1);
}
)AHFL";

    auto result = typecheck_source("fn_explicit_generic_call.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    const auto *id_symbol = find_function_symbol(result, "id");
    REQUIRE(id_symbol != nullptr);

    bool found_id_int_call = false;
    for (const auto &site : result.typed_program.fn_call_sites) {
        if (site.fn_symbol == id_symbol->id && site.type_args.size() == 1 &&
            site.type_args.front() != nullptr && site.type_args.front()->describe() == "Int") {
            found_id_int_call = true;
            break;
        }
    }
    CHECK(found_id_int_call);

    auto &types = ahfl::TypeContext::global();
    const auto mono = ahfl::run_monomorphization(result.typed_program, types);
    bool found_id_int_instance = false;
    for (const auto &inst : mono.instances) {
        if (inst.decl_canonical_name.find("id") != std::string::npos &&
            inst.type_args_canonical.find("Int") != std::string::npos) {
            found_id_int_instance = true;
            break;
        }
    }
    CHECK(found_id_int_instance);
}

TEST_CASE("@builtin attributes are rejected outside std modules") {
    const std::string source = R"AHFL(
module app::main;

@builtin("string_raw_length")
fn length(s: String) -> Int effect Pure;
)AHFL";

    auto result = typecheck_source("fn_invalid_builtin.ahfl", source);
    REQUIRE(result.has_errors());

    bool saw_invalid_builtin = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == "typecheck.INVALID_BUILTIN_ATTRIBUTE") {
            saw_invalid_builtin = true;
            CHECK(entry.message.find("@builtin is only allowed in std modules") !=
                  std::string::npos);
        }
    }
    CHECK(saw_invalid_builtin);
}

TEST_CASE("@builtin attributes require known hooks") {
    const std::string source = R"AHFL(
module std::custom;

@builtin("not_a_registered_hook")
fn mystery() -> Int effect Pure;
)AHFL";

    auto result = typecheck_source("fn_unknown_builtin.ahfl", source);
    REQUIRE(result.has_errors());

    bool saw_unknown_builtin = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == "typecheck.UNKNOWN_BUILTIN_HOOK") {
            saw_unknown_builtin = true;
            CHECK(entry.message.find("unknown @builtin hook") != std::string::npos);
        }
    }
    CHECK(saw_unknown_builtin);
}

TEST_CASE("@builtin attributes require explicit effect clauses") {
    const std::string source = R"AHFL(
module std::custom;

@builtin("string_raw_length")
fn length(s: String) -> Int;
)AHFL";

    auto result = typecheck_source("fn_builtin_missing_effect.ahfl", source);
    REQUIRE(result.has_errors());

    bool saw_missing_effect = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == "typecheck.MISSING_BUILTIN_EFFECT") {
            saw_missing_effect = true;
            CHECK(entry.message.find("must declare an explicit effect clause") !=
                  std::string::npos);
        }
    }
    CHECK(saw_missing_effect);
}

// ---------------------------------------------------------------------------
// TC6b (P2 acceptance): closures are first-class Fn values. A lambda passed to
// a `Fn(Int) -> Int` parameter must typecheck as that Fn type, and the callee
// body may call the parameter as a callable value (`f(x)`).
// ---------------------------------------------------------------------------
TEST_CASE("Lambda can be passed to Fn parameter and called through that parameter") {
    const std::string source = R"AHFL(
fn apply(x: Int, f: Fn(Int) -> Int) -> Int effect Pure decreases 0 {
    return f(x);
}

fn caller() -> Int effect Pure decreases 0 {
    return apply(1, \n: Int -> n + 1);
}
)AHFL";

    const auto result = typecheck_source("fn_lambda_as_fn_argument.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
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

// ---------------------------------------------------------------------------
// TC9 (P2b): Fn body type-check — parameter binding and return value type
// checking work end-to-end. A function whose body is well-typed typechecks
// clean and the body's TypedBlock index is recorded on FnTypeInfo.
// ---------------------------------------------------------------------------
TEST_CASE("Fn body typechecks with correct return type") {
    const std::string source = R"AHFL(
fn double(x: Int) -> Int effect Pure decreases 0 {
    return x + x;
}
)AHFL";

    const auto result = typecheck_source("fn_body_ok.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    // Verify body_block_index is populated on the FnTypeInfo.
    const auto *symbol = find_function_symbol(result, "double");
    REQUIRE(symbol != nullptr);
    const auto fn_info = result.environment.get_fn(symbol->id);
    REQUIRE(fn_info.has_value());
    CHECK(fn_info->get().body_block_index != UINT32_MAX);
    CHECK(fn_info->get().body_block_index < result.typed_program.blocks.size());
}

// ---------------------------------------------------------------------------
// TC10 (P2b): Fn body return-type mismatch produces a type error. The
// type-checker must catch that the returned expression type does not match
// the declared return type.
// ---------------------------------------------------------------------------
TEST_CASE("Fn body with wrong return type produces error") {
    const std::string source = R"AHFL(
fn bad_return(x: Int) -> String effect Pure decreases 0 {
    return x + 1;
}
)AHFL";

    const auto result = typecheck_source("fn_bad_return.ahfl", source);
    CHECK(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC11 (P2b): Fn body effect-underdeclared check — a function declared Pure
// whose body calls a Nondet function triggers the EffectUnderdeclared
// diagnostic.
// ---------------------------------------------------------------------------
TEST_CASE("Fn body effect underdeclared is diagnosed") {
    const std::string source = R"AHFL(
fn nondet_source() -> Int effect Nondet {
    return 42;
}

fn pure_but_cheats() -> Int effect Pure decreases 0 {
    return nondet_source();
}
)AHFL";

    const auto result = typecheck_source("fn_underdeclared.ahfl", source);
    CHECK(result.has_errors());
    // Verify at least one diagnostic is an effect-related error.
    bool found = false;
    for (const auto &d : result.diagnostics.entries()) {
        if (d.severity == ahfl::DiagnosticSeverity::Error &&
            d.message.find("declares effect") != std::string::npos &&
            d.message.find("body infers effect") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ---------------------------------------------------------------------------
// TC12 (P2b): Multiple fn declarations with bodies all get their bodies
// type-checked and their body_block_index populated.
// ---------------------------------------------------------------------------
TEST_CASE("Multiple fn bodies are all typechecked") {
    const std::string source = R"AHFL(
fn add(a: Int, b: Int) -> Int effect Pure decreases 0 {
    return a + b;
}

fn negate(x: Int) -> Int effect Pure decreases 0 {
    return 0 - x;
}
)AHFL";

    const auto result = typecheck_source("fn_multi_body.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    for (const auto &name : {"add", "negate"}) {
        const auto *symbol = find_function_symbol(result, name);
        REQUIRE(symbol != nullptr);
        const auto fn_info = result.environment.get_fn(symbol->id);
        REQUIRE(fn_info.has_value());
        CHECK(fn_info->get().body_block_index != UINT32_MAX);
    }
}

// ---------------------------------------------------------------------------
// TC13 (P2b): Fn with no body (prototype / declaration only) is skipped by
// FnSema — no body, no type-checking needed, no errors from missing body.
// ---------------------------------------------------------------------------
TEST_CASE("Fn prototype without body is skipped by FnSema") {
    const std::string source = R"AHFL(
fn proto(x: Int) -> Int effect Pure;
)AHFL";

    const auto result = typecheck_source("fn_prototype.ahfl", source);
    CHECK_FALSE(result.has_errors());

    const auto *symbol = find_function_symbol(result, "proto");
    REQUIRE(symbol != nullptr);
    const auto fn_info = result.environment.get_fn(symbol->id);
    REQUIRE(fn_info.has_value());
    CHECK_FALSE(fn_info->get().has_body);
    CHECK(fn_info->get().body_block_index == UINT32_MAX);
}

// ===========================================================================
// P2d tests: monomorphization + type substitution + body instantiation
// ===========================================================================

// ---------------------------------------------------------------------------
// TC14 (P2d): substitute_type replaces a TypeVar with a concrete type.
// ---------------------------------------------------------------------------
TEST_CASE("substitute_type replaces TypeVar with concrete type") {
    auto &types = ahfl::TypeContext::global();
    const auto int_type = types.make(ahfl::TypeKind::Int);

    ahfl::TypeSubstitutionMap subst;
    subst.push_back(int_type);

    // TypeVar at index 0 ("T") should become Int.
    const auto t_var = types.type_var(0, "T");
    const auto replaced = ahfl::substitute_type(t_var, subst, types);
    CHECK(replaced == int_type);
    CHECK(replaced->describe() == "Int");

    // TypeVar at index 1 ("U") not in map stays as TypeVar.
    const auto u_var = types.type_var(1, "U");
    const auto kept = ahfl::substitute_type(u_var, subst, types);
    CHECK(kept->describe() == "U");

    // Nullptr passes through.
    CHECK(ahfl::substitute_type(nullptr, subst, types) == nullptr);
}

TEST_CASE("substitute_type recurses into fn types") {
    auto &types = ahfl::TypeContext::global();
    const auto int_type = types.make(ahfl::TypeKind::Int);

    ahfl::TypeSubstitutionMap subst;
    subst.push_back(int_type);

    // Fn(T) -> T becomes Fn(Int) -> Int
    std::vector<ahfl::TypePtr> params = {types.type_var(0, "T")};
    const auto fn_t =
        types.fn(std::move(params), types.type_var(0, "T"), ahfl::EffectJudgement::make_pure());
    const auto fn_int = ahfl::substitute_type(fn_t, subst, types);
    CHECK(fn_int->describe().find("Int") != std::string::npos);
}

TEST_CASE("substitute_type recurses into nominal std container type arguments") {
    auto &types = ahfl::TypeContext::global();
    const auto int_type = types.make(ahfl::TypeKind::Int);
    const auto t_var = types.type_var(0, "T");

    ahfl::TypeSubstitutionMap subst;
    subst.push_back(int_type);

    const auto option_t = types.enum_type(
        "std::option::Option", std::optional<ahfl::SymbolId>{}, std::vector{t_var});
    const auto option_int = ahfl::substitute_type(option_t, subst, types);
    CHECK(option_int->describe() == "std::option::Option<Int>");

    const auto list_t = types.struct_type(
        "std::collections::List", std::optional<ahfl::SymbolId>{}, std::vector{t_var});
    const auto list_int = ahfl::substitute_type(list_t, subst, types);
    CHECK(list_int->describe() == "std::collections::List<Int>");

    const auto set_t = types.struct_type(
        "std::collections::Set", std::optional<ahfl::SymbolId>{}, std::vector{t_var});
    const auto set_int = ahfl::substitute_type(set_t, subst, types);
    CHECK(set_int->describe() == "std::collections::Set<Int>");

    const auto map_tt = types.struct_type("std::collections::Map",
                                          std::optional<ahfl::SymbolId>{},
                                          std::vector{t_var, t_var});
    const auto map_int = ahfl::substitute_type(map_tt, subst, types);
    CHECK(map_int->describe() == "std::collections::Map<Int, Int>");
}

// ---------------------------------------------------------------------------
// TC16 (P2d): run_monomorphization with TypeContext sets body_block_index
// for non-generic fns (they reuse the original body directly).
// ---------------------------------------------------------------------------
TEST_CASE("Monomorphization P2d sets body_block_index for non-generic fns") {
    const std::string source = R"AHFL(
fn add(a: Int, b: Int) -> Int effect Pure decreases 0 {
    return a + b;
}

fn negate(x: Int) -> Int effect Pure decreases 0 {
    return 0 - x;
}

// Call the fns so they appear in fn_call_sites.
fn caller() -> Int effect Pure decreases 0 {
    return add(1, 2) + negate(3);
}
)AHFL";

    auto result = typecheck_source("fn_mono_p2d.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    auto &types = ahfl::TypeContext::global();
    const auto mono = ahfl::run_monomorphization(result.typed_program, types);

    // Should have instances for add, negate, caller.
    CHECK(mono.instances.size() >= 2);

    // Each instance with a body should have body_block_index set.
    std::size_t with_body = 0;
    for (const auto &inst : mono.instances) {
        if (inst.body_block_index != UINT32_MAX) {
            ++with_body;
            CHECK(inst.body_block_index < result.typed_program.blocks.size());
        }
    }
    CHECK(with_body >= 2);
}

// ---------------------------------------------------------------------------
// TC17 (P2d): instantiate_fn_body deep-clones a generic fn body and
// substitutes all TypeVars with concrete types.
//
// We typecheck a generic identity fn `id<T>(x: T) -> T { return x; }`, then
// manually build a substitution map and call instantiate_fn_body directly.
// The resulting body's let/return types should all be concrete (Int), not
// TypeVar "T".
// ---------------------------------------------------------------------------
TEST_CASE("instantiate_fn_body substitutes TypeVars in cloned body") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure decreases 0 {
    return x;
}
)AHFL";

    auto result = typecheck_source("fn_id_generic.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    const auto *symbol = find_function_symbol(result, "id");
    REQUIRE(symbol != nullptr);
    const auto fn_info = result.environment.get_fn(symbol->id);
    REQUIRE(fn_info.has_value());
    CHECK(fn_info->get().type_param_names.size() == 1);
    CHECK(fn_info->get().type_param_names[0] == "T");
    const auto orig_body_idx = fn_info->get().body_block_index;
    CHECK(orig_body_idx != UINT32_MAX);
    CHECK(orig_body_idx < result.typed_program.blocks.size());

    // Verify the original body has statements and TypeVar types.
    // Take the size by value (not reference) to avoid vector-reallocation UB
    // after instantiate_fn_body appends new blocks.
    const auto orig_stmt_count =
        result.typed_program.blocks[orig_body_idx].statement_indexes.size();
    REQUIRE(orig_stmt_count > 0);

    // Instantiate with T -> Int (T is index 0, the first type param).
    auto &types = ahfl::TypeContext::global();
    ahfl::TypeSubstitutionMap subst;
    subst.push_back(types.make(ahfl::TypeKind::Int));

    const auto inst = ahfl::instantiate_fn_body(result.typed_program, orig_body_idx, subst, types);

    CHECK(inst.body_block_index != UINT32_MAX);
    CHECK(inst.body_block_index != orig_body_idx);
    CHECK(inst.body_block_index < result.typed_program.blocks.size());
    CHECK(inst.block_count > 0);
    CHECK(inst.stmt_count > 0);

    // The cloned block should have the same number of statements as the original.
    const auto &clone_block = result.typed_program.blocks[inst.body_block_index];
    CHECK(clone_block.statement_indexes.size() == orig_stmt_count);

    // Original body should still have the same number of statements (untouched).
    CHECK(result.typed_program.blocks[orig_body_idx].statement_indexes.size() == orig_stmt_count);

    // All expression types in the cloned body should be Int (no TypeVars left).
    bool found_int = false;
    for (const auto stmt_idx : clone_block.statement_indexes) {
        const auto &stmt = result.typed_program.statements[stmt_idx];
        // Check let type annotation if present.
        if (stmt.let_type != nullptr) {
            CHECK(stmt.let_type->describe() != "T");
            if (stmt.let_type->describe() == "Int") {
                found_int = true;
            }
        }
        // Check child expression types.
        for (const auto expr_idx : stmt.children_expr_index) {
            if (expr_idx == UINT32_MAX)
                continue;
            const auto &expr = result.typed_program.expressions[expr_idx];
            if (expr.type != nullptr) {
                CHECK(expr.type->describe() != "T");
                if (expr.type->describe() == "Int") {
                    found_int = true;
                }
            }
        }
    }
    // With `return x;` the return value references the param `x`; the
    // expression's type should have been substituted to Int.
    CHECK(found_int);

    // Original body should be untouched (still has TypeVar "T").
    // Re-lookup by index after instantiation (vector may have reallocated).
    bool orig_has_typevar = false;
    const auto &orig_stmts = result.typed_program.blocks[orig_body_idx].statement_indexes;
    for (const auto stmt_idx : orig_stmts) {
        const auto &stmt = result.typed_program.statements[stmt_idx];
        for (const auto expr_idx : stmt.children_expr_index) {
            if (expr_idx == UINT32_MAX)
                continue;
            const auto &expr = result.typed_program.expressions[expr_idx];
            if (expr.type != nullptr && expr.type->describe() == "T") {
                orig_has_typevar = true;
                break;
            }
        }
        if (orig_has_typevar)
            break;
    }
    CHECK(orig_has_typevar);
}

// ---------------------------------------------------------------------------
// TC18 (P2d): Repeated instantiations with the same substitution are not
// deduped at the instantiate_fn_body level — dedup happens at the
// run_monomorphization pass level via the instance cache. Each call to
// instantiate_fn_body produces a fresh clone (the caller owns dedup).
// ---------------------------------------------------------------------------
TEST_CASE("instantiate_fn_body produces fresh clone each call") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure decreases 0 {
    return x;
}
)AHFL";

    auto result = typecheck_source("fn_id_dedup.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    const auto *symbol = find_function_symbol(result, "id");
    REQUIRE(symbol != nullptr);
    const auto fn_info = result.environment.get_fn(symbol->id);
    REQUIRE(fn_info.has_value());

    auto &types = ahfl::TypeContext::global();
    ahfl::TypeSubstitutionMap subst;
    subst.push_back(types.make(ahfl::TypeKind::Int));

    const auto before_blocks = result.typed_program.blocks.size();

    const auto inst1 = ahfl::instantiate_fn_body(
        result.typed_program, fn_info->get().body_block_index, subst, types);
    const auto inst2 = ahfl::instantiate_fn_body(
        result.typed_program, fn_info->get().body_block_index, subst, types);

    // Two independent clones at different indexes.
    CHECK(inst1.body_block_index != inst2.body_block_index);
    CHECK(inst1.block_count == inst2.block_count);
    CHECK(result.typed_program.blocks.size() ==
          before_blocks + inst1.block_count + inst2.block_count);
}

// ---------------------------------------------------------------------------
// TC19 (P2d): Monomorphization pass produces deduped instances — two call
// sites of the same (fn, type_args) produce one instance, not two.
//
// This non-generic case exercises the same cache path as generic dedup without
// depending on generic type-argument inference.
// ---------------------------------------------------------------------------
TEST_CASE("Monomorphization dedups identical call sites") {
    const std::string source = R"AHFL(
fn double(x: Int) -> Int effect Pure decreases 0 {
    return x + x;
}

fn caller1() -> Int effect Pure decreases 0 {
    return double(1);
}

fn caller2() -> Int effect Pure decreases 0 {
    return double(2) + double(3);
}
)AHFL";

    auto result = typecheck_source("fn_mono_dedup.ahfl", source);
    REQUIRE_FALSE(result.has_errors());

    // double is called 3 times across caller1 and caller2, but should
    // produce only 1 instance (non-generic, all type_args empty).
    auto &types = ahfl::TypeContext::global();
    const auto mono = ahfl::run_monomorphization(result.typed_program, types);

    // Find the instance for `double`.
    bool found_double = false;
    std::string double_canonical;
    for (const auto &inst : mono.instances) {
        if (inst.decl_canonical_name.find("double") != std::string::npos) {
            found_double = true;
            double_canonical = inst.decl_canonical_name;
            CHECK(inst.body_block_index != UINT32_MAX);
            break;
        }
    }
    CHECK(found_double);

    // instances_per_decl for double should be exactly 1 (dedup works).
    const auto it = mono.instances_per_decl.find(double_canonical);
    REQUIRE(it != mono.instances_per_decl.end());
    CHECK(it->second == 1);
}

// =============================================================================
// B-2 (RFC 0013 slice P3-gaps-A, Gap 3): closure parameter parser gaps.
//
// Pins the three Gap-3 surfaces:
//   1. lambdaParam uses the keyword-permissive `identifier` rule, so `self`,
//      `map`, `set`, ... are legal closure parameter names;
//   2. tparam scoping inside impl/method bodies reaches closure bodies
//      (impl-level T and method-level U), with value/type namespace
//      separation so a lambda param named `T` coexists with a tparam `T`;
//   3. a closure parameter with no annotation and no expected Fn type halts
//      with typecheck.CANNOT_INFER_CLOSURE_PARAM at the parameter's
//      SourceRange instead of silently producing an error type that crashes
//      at the IR lowering boundary.
//
// Shadowing rule (test gap 3): a lambda parameter named `self` SHADOWS the
// impl-method receiver `self` inside the closure body. The child value context
// is copied from the method body (which carries the receiver binding) and the
// parameter is then insert_or_assign-ed, so the parameter wins. T7 pins this
// by giving the receiver and the parameter different types: if the receiver
// won, the lambda body type would mismatch the expected Fn(T) -> T return.
//
// Harness mirrors c4_capture_list.cpp: parse_project -> resolve -> typecheck.
//
// Coverage: 7 TEST_CASE / 30+ assertions.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/opt/opt_ir.hpp"
#include "ahfl/compiler/backends/driver.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "runtime/evaluator/executor.hpp"
#include "runtime/evaluator/runtime_fn_table.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"
#include "runtime/engine/response_schema_validator.hpp"
#include "tooling/formatter/formatter.hpp"

#include "common/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ahfl::test_support::diagnostic_count_with_code;

void write_file(const std::filesystem::path &path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs{path, std::ios::binary | std::ios::trunc};
    REQUIRE(ofs.good());
    ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(ofs.good());
}

struct CompileArtifacts {
    std::filesystem::path root;
    ahfl::ProjectParseResult parse;
    ahfl::ResolveResult resolve;
    ahfl::TypeCheckResult tc;
};

// Full parse_project -> resolve -> typecheck pipeline over a single-entry
// app module with the repo std available. Surfaces diagnostics via MESSAGE so
// an unexpected failure gives an actionable cause.
[[nodiscard]] CompileArtifacts compile_project_loose(std::string_view filename,
                                                     std::string_view source) {
    const std::string sanitized = [filename] {
        std::string s{filename};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_b2_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    write_file(main_path, std::string{source});

    const ahfl::Frontend frontend;
    a.parse = ahfl::parse_project(frontend,
                                  ahfl::test_support::project_input_with_repo_std_for_test_file(
                                      main_path, a.root, __FILE__, false));

    std::size_t parse_err_count = 0;
    if (a.parse.has_errors()) {
        MESSAGE("[B2-PARSE] ", filename, " diagnostics:");
        for (const auto &d : a.parse.diagnostics.entries()) {
            MESSAGE("  [", to_string(d.severity), "] ", d.message.c_str());
            if (d.severity == ahfl::DiagnosticSeverity::Error) {
                ++parse_err_count;
            }
        }
    }
    REQUIRE_EQ(parse_err_count, 0u);
    REQUIRE_FALSE(a.parse.graph.entry_sources.empty());

    const ahfl::Resolver resolver;
    a.resolve = resolver.resolve(a.parse.graph);

    const ahfl::TypeChecker type_checker;
    a.tc = type_checker.check(a.parse.graph, a.resolve, {});
    return a;
}

// Counts diagnostics whose code string contains `substr` (e.g. "UNKNOWN_TYPE"
// matches "typecheck.UNKNOWN_TYPE").
[[nodiscard]] std::size_t diag_count_containing(const ahfl::DiagnosticBag &bag,
                                                std::string_view substr) {
    std::size_t count = 0;
    for (const auto &d : bag.entries()) {
        const std::string code = d.code.value_or(std::string{});
        if (code.find(substr) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

// First diagnostic carrying exactly `code` (e.g. "typecheck.CANNOT_INFER_CLOSURE_PARAM").
[[nodiscard]] const ahfl::Diagnostic *find_diag_with_code(const ahfl::DiagnosticBag &bag,
                                                          std::string_view code) {
    for (const auto &d : bag.entries()) {
        if (d.code.has_value() && *d.code == code) {
            return &d;
        }
    }
    return nullptr;
}

// Byte offset of the first occurrence of `needle` in `haystack`; fails the
// test when absent. Used to pin a diagnostic's SourceRange against the exact
// source bytes that were written to the entry file.
[[nodiscard]] std::size_t byte_offset_of(const std::string &haystack, std::string_view needle) {
    const auto pos = haystack.find(needle);
    REQUIRE(pos != std::string::npos);
    return pos;
}

} // namespace

// ============================================================================
// T1 (Step 4a): `\(y: T) -> y` inside an `impl<T>` method body sees the
// impl-level type parameter T. Pins the existing tparam-visibility behaviour
// for closure bodies (the C-5 capture-list test proves the resolver side;
// this pins the plain-lambda typecheck side).
// ============================================================================
TEST_CASE("B-2 lambda in impl<T> method body sees impl-level T") {
    const auto a = compile_project_loose("t1_impl_body_T",
                                         R"AHFL(
        module b2::t1;
        trait Identity<T> {
            fn app(self: Self, f: Fn(T) -> T) -> T effect Pure;
        }
        struct Holder<T> {
            value: T;
        }
        impl<T> Identity<T> for Holder<T> {
            fn app(self: Self, f: Fn(T) -> T) -> T effect Pure decreases 0 {
                let passthrough: Fn(T) -> T = \(y: T) -> y;
                return f(passthrough(self.value));
            }
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// T2 (Step 4b): `\(y: U) -> y` inside `impl<T> fn morph<U>` sees the
// METHOD-level type parameter U (the body scope is [impl tparams] ++ [method
// tparams], so U resolves even though the impl head only declares T).
// ============================================================================
TEST_CASE("B-2 lambda in impl<T> method body sees method-level U") {
    const auto a = compile_project_loose("t2_impl_body_method_U",
                                         R"AHFL(
        module b2::t2;
        struct Box<T> {
            value: T;
        }
        impl<T> Box<T> {
            fn morph<U>(self: Box<T>, u: U) -> U effect Pure decreases 0 {
                let g: Fn(U) -> U = \(y: U) -> y;
                return g(u);
            }
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// T3 (Step 4c): a lambda PARAMETER named `T` coexists with a tparam `T`.
// Value/type namespace separation: `T` in the param-name slot (and in the
// body's value position) is the value binding, while `T` in a type-annotation
// slot is the type parameter. The two never collide.
// ============================================================================
TEST_CASE("B-2 lambda param named T coexists with tparam T") {
    const auto a = compile_project_loose("t3_param_named_T",
                                         R"AHFL(
        module b2::t3;
        fn pick<T>(x: T) -> Int effect Pure decreases 0 {
            let f: Fn(Int) -> Int = \(T: Int) -> T;
            return f(1);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// T4 (Step 4d): `\(self: T) -> self` parses and typechecks inside a free
// generic fn (no receiver in scope, so `self` is just an ordinary param name).
// ============================================================================
TEST_CASE("B-2 self is a legal lambda param name") {
    const auto a = compile_project_loose("t4_self_param",
                                         R"AHFL(
        module b2::t4;
        fn id<T>(x: T) -> T effect Pure decreases 0 {
            let f: Fn(T) -> T = \(self: T) -> self;
            return f(x);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// T5 (Step 4e, test gap 8): `let g = \(x) -> x;` with NO annotation and NO
// expected Fn type must produce typecheck.CANNOT_INFER_CLOSURE_PARAM at the
// parameter's SourceRange — not a silent error type that later crashes at the
// IR lowering boundary. Asserts BOTH the code AND that the diagnostic range
// equals the param's range (Principle 5).
// ============================================================================
TEST_CASE("B-2 un-inferrable closure param emits CANNOT_INFER_CLOSURE_PARAM at param range") {
    const std::string source = R"AHFL(
        module b2::t5;
        fn main() -> Int effect Pure decreases 0 {
            let g = \(x) -> x;
            return 0;
        }
        )AHFL";
    const auto a = compile_project_loose("t5_cannot_infer", source);
    REQUIRE(a.tc.has_errors());

    const auto *diag =
        find_diag_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM");
    REQUIRE(diag != nullptr);
    // The message names the parameter and suggests an explicit annotation or
    // an expected Fn type.
    CHECK(diag->message.find("'x'") != std::string::npos);
    CHECK(diag->message.find("annotation") != std::string::npos);

    // The diagnostic range is exactly the lambda parameter `x` (the first
    // `x` after `\(`), not the whole lambda or the let statement.
    REQUIRE(diag->range.has_value());
    const auto param_begin = byte_offset_of(source, "\\(x)") + 2; // skip `\(`
    CHECK_EQ(diag->range->begin_offset, param_begin);
    CHECK_EQ(diag->range->end_offset, param_begin + 1);
}

// ============================================================================
// T6 (Step 4f, test gap 5): keyword lambda params beyond `self` — `map` and
// `set` are legal parameter names once lambdaParam uses the keyword-permissive
// `identifier` rule.
//
// Grammar note: `map`/`set` are keyword tokens, not IDENT, and pathRoot only
// admits IDENT/'input'/'output'/'self', so a keyword-named param cannot be
// referenced by bare name in a body (that pathRoot limitation is out of scope
// for this slice). This test therefore pins the DECLARATION surface — the
// param parses with its annotated type and the lambda gets the expected Fn
// type — using a literal body.
// ============================================================================
TEST_CASE("B-2 map and set are legal lambda param names") {
    const auto a = compile_project_loose("t6_keyword_params",
                                         R"AHFL(
        module b2::t6;
        fn main() -> Int effect Pure decreases 0 {
            let f: Fn(Int) -> Int = \(map: Int) -> 1;
            let g: Fn(Int) -> Int = \(set: Int) -> 2;
            return f(1) + g(2);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// T7 (Step 4g, test gap 3): `\(self: T) -> self` NESTED inside a method with a
// `self` receiver.
//
// SHADOWING RULE (pinned here): the lambda parameter `self` SHADOWS the
// impl-method receiver `self` inside the closure body. The child value context
// is copied from the method body (which carries the receiver binding of type
// Box<T>) and the parameter is then insert_or_assign-ed, so the parameter
// (type T) wins. If the receiver won instead, the body type would be Box<T>
// and the lambda would mismatch the expected Fn(T) -> T return — so asserting
// a clean typecheck pins the param-shadows-receiver rule.
// ============================================================================
TEST_CASE("B-2 lambda param self shadows method receiver self") {
    const auto a = compile_project_loose("t7_self_shadows_receiver",
                                         R"AHFL(
        module b2::t7;
        struct Box<T> {
            value: T;
        }
        impl<T> Box<T> {
            fn echo(self: Box<T>, x: T) -> T effect Pure decreases 0 {
                let f: Fn(T) -> T = \(self: T) -> self;
                return f(x);
            }
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.CANNOT_INFER_CLOSURE_PARAM"),
             0u);
}

// ============================================================================
// P3-gaps-B (RFC 0013): wildcard `let _ = e;` + `{}` unit literal.
//
// Gap 1: wildcard let — the binder `_` creates NO value binding, so:
//   - no SHADOWED_BINDING warning is emitted;
//   - multiple wildcards in the same scope do not collide;
//   - the resolver skips add_value_binding for `_`;
//   - `_` is not a valid IDENT token, so referencing `_` is a parse error.
//
// Gap 2: `{}` unit literal — the sole inhabitant of the Unit type:
//   - typechecks as Unit in let/return/argument/lambda/match-arm positions;
//   - rejected by TYPE_MISMATCH where Unit is not expected;
//   - const-evaluable (B6): `const X: Unit = {};` succeeds;
//   - lowers to ir::UnitLiteralExpr, then to an SSA monostate constant (B2);
//   - evaluates to evaluator::UnitValue at runtime;
//   - serialises as JSON null (B3);
//   - accepted by the response-schema validator against a Unit schema (B3).
//
// B5 (ADOPTED): wildcard lets lower to ir::ExprStatement at the HIR->IR
// boundary, so the executor never sees a wildcard LetStatement — the
// initializer is evaluated for effects only and the result is discarded.
// ============================================================================

namespace {

// --- Runtime harness (mirrors evaluator_generics.cpp) ----------------------

struct RuntimeProgram {
    ahfl::ParseResult parse_result;
    ahfl::ResolveResult resolve_result;
    ahfl::TypeCheckResult typecheck_result;
    ahfl::ir::Program program_ir;
    ahfl::evaluator::RuntimeFunctionTable fn_table;
};

[[nodiscard]] std::optional<RuntimeProgram> compile_runtime(const std::string &filename,
                                                             const std::string &source) {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(filename, source);
    if (parse_result.has_errors() || parse_result.program == nullptr) {
        MESSAGE("parse errors in " << filename);
        for (const auto &entry : parse_result.diagnostics.entries()) {
            MESSAGE("  parse: " << entry.message);
        }
        return std::nullopt;
    }

    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        MESSAGE("resolve errors in " << filename);
        for (const auto &entry : resolve_result.diagnostics.entries()) {
            MESSAGE("  resolve: " << entry.message);
        }
        return std::nullopt;
    }

    const ahfl::TypeChecker type_checker;
    auto typecheck_result = type_checker.check(*parse_result.program, resolve_result);
    if (typecheck_result.has_errors()) {
        MESSAGE("typecheck errors in " << filename);
        for (const auto &entry : typecheck_result.diagnostics.entries()) {
            MESSAGE("  typecheck: " << entry.message);
        }
        return std::nullopt;
    }

    auto program_ir =
        ahfl::lower_program_ir(*parse_result.program, resolve_result, typecheck_result);
    ahfl::evaluator::RuntimeFunctionTable fn_table(program_ir);

    return RuntimeProgram{
        std::move(parse_result),
        std::move(resolve_result),
        std::move(typecheck_result),
        std::move(program_ir),
        std::move(fn_table),
    };
}

[[nodiscard]] const ahfl::ir::Block *find_fn_body(const RuntimeProgram &c,
                                                   std::string_view fn_name) {
    for (const auto &decl : c.program_ir.declarations) {
        const auto *fn = std::get_if<ahfl::ir::FnDecl>(&decl);
        if (fn == nullptr) continue;
        if (fn->name == fn_name || fn->symbol_ref.canonical_name == fn_name) {
            return fn->body.get();
        }
        auto ends_with = [](std::string_view haystack, std::string_view needle) {
            if (needle.size() > haystack.size()) return false;
            return haystack.substr(haystack.size() - needle.size()) == needle;
        };
        if (ends_with(fn->name, fn_name) || ends_with(fn->symbol_ref.canonical_name, fn_name)) {
            return fn->body.get();
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<ahfl::evaluator::Value>
run_caller(const RuntimeProgram &c, std::string_view caller_name = "caller") {
    const ahfl::ir::Block *body = find_fn_body(c, caller_name);
    if (body == nullptr) {
        MESSAGE("could not locate body of fn '" << std::string(caller_name) << "'");
        return std::nullopt;
    }

    ahfl::evaluator::ExecContext ctx;
    ahfl::evaluator::RuntimeFnTrace trace_cfg;
    c.fn_table.install(ctx, trace_cfg);

    const ahfl::evaluator::ExecResult r = ahfl::evaluator::exec_block(*body, ctx);
    if (r.has_errors()) {
        for (const auto &entry : r.diagnostics.entries()) {
            MESSAGE("exec diagnostic: " << entry.message);
        }
        return std::nullopt;
    }
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&r.outcome);
    if (ret == nullptr) {
        MESSAGE("caller body did not return a value");
        return std::nullopt;
    }
    return ahfl::evaluator::clone_value(ret->value);
}

// --- Opt-IR helpers (mirrors opt_ir.cpp) ------------------------------------

void add_simple_flow(ahfl::ir::Program &program, std::vector<ahfl::ir::StatementPtr> stmts) {
    ahfl::ir::Block body;
    body.statements = std::move(stmts);

    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";
    handler.body = std::move(body);

    ahfl::ir::FlowDecl flow;
    flow.target_ref.canonical_name = "TestAgent";
    flow.state_handlers.push_back(std::move(handler));

    program.declarations.push_back(std::move(flow));
}

[[nodiscard]] ahfl::ir::TypeRef unit_type_ref() {
    ahfl::ir::TypeRef type;
    type.kind = ahfl::ir::TypeRefKind::Unit;
    type.display_name = "Unit";
    return type;
}

[[nodiscard]] const ahfl::ir::opt::OptFunction *
find_opt_function(const ahfl::ir::opt::OptProgram &program, const std::string &name) {
    const auto it = std::find_if(program.functions.begin(),
                                 program.functions.end(),
                                 [&name](const auto &function) { return function.name == name; });
    return it == program.functions.end() ? nullptr : &*it;
}

} // namespace

// ============================================================================
// Gap 1: wildcard let
// ============================================================================

TEST_CASE("P3-gaps-B wildcard let typechecks without SHADOWED_BINDING") {
    const auto a = compile_project_loose("wildcard_let",
                                         R"AHFL(
        module b2::wildcard_let;
        fn main() -> Int effect Pure decreases 0 {
            let _ = 42;
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.SHADOWED_BINDING"), 0u);
}

TEST_CASE("P3-gaps-B wildcard let with annotation") {
    const auto a = compile_project_loose("wildcard_let_annotated",
                                         R"AHFL(
        module b2::wildcard_let_annotated;
        fn main() -> Int effect Pure decreases 0 {
            let _: Int = 42;
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.SHADOWED_BINDING"), 0u);
}

TEST_CASE("P3-gaps-B multiple wildcards do not collide") {
    const auto a = compile_project_loose("wildcard_let_multiple",
                                         R"AHFL(
        module b2::wildcard_let_multiple;
        fn main() -> Int effect Pure decreases 0 {
            let _ = 1;
            let _ = 2;
            let _ = 3;
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.SHADOWED_BINDING"), 0u);
}

TEST_CASE("P3-gaps-B wildcard let evaluates initializer for effects only") {
    // B5: the wildcard let lowers to ir::ExprStatement, so the initializer
    // is evaluated but the result is discarded. A pure initializer must not
    // produce any diagnostic.
    const auto a = compile_project_loose("wildcard_let_effects",
                                         R"AHFL(
        module b2::wildcard_let_effects;
        fn id(x: Int) -> Int effect Pure decreases 0 { return x; }
        fn main() -> Int effect Pure decreases 0 {
            let _ = id(42);
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B underscore is not referenceable as a value") {
    // `_` is NOT a valid IDENT token (IDENT must start with LETTER), so
    // `let x = _;` is a PARSE error, not a semantic error.
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text(
        "underscore_ref.ahfl",
        R"AHFL(
        fn main() -> Int effect Pure decreases 0 {
            let x = _;
            return 0;
        }
        )AHFL");
    CHECK(parse_result.has_errors());
}

// ============================================================================
// Gap 2: unit literal `{}`
// ============================================================================

TEST_CASE("P3-gaps-B let u: Unit = {} typechecks") {
    const auto a = compile_project_loose("unit_let",
                                         R"AHFL(
        module b2::unit_let;
        fn main() -> Int effect Pure decreases 0 {
            let u: Unit = {};
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B return {} typechecks") {
    const auto a = compile_project_loose("unit_return",
                                         R"AHFL(
        module b2::unit_return;
        fn f() -> Unit effect Pure decreases 0 {
            return {};
        }
        fn main() -> Int effect Pure decreases 0 {
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B unit argument typechecks") {
    const auto a = compile_project_loose("unit_arg",
                                         R"AHFL(
        module b2::unit_arg;
        fn take_unit(u: Unit) -> Int effect Pure decreases 0 { return 1; }
        fn main() -> Int effect Pure decreases 0 {
            return take_unit({});
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B lambda returning unit typechecks") {
    const auto a = compile_project_loose("unit_lambda",
                                         R"AHFL(
        module b2::unit_lambda;
        fn main() -> Int effect Pure decreases 0 {
            let f: Fn() -> Unit = \() -> {};
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B match arm returning unit typechecks") {
    const auto a = compile_project_loose("unit_match",
                                         R"AHFL(
        module b2::unit_match;
        import std::option;
        fn f(x: option::Option<Int>) -> Unit effect Pure decreases 0 {
            return match x {
                _ => {}
            };
        }
        fn main() -> Int effect Pure decreases 0 {
            return 0;
        }
        )AHFL");
    if (a.tc.has_errors()) {
        for (const auto &d : a.tc.diagnostics.entries()) {
            MESSAGE("  tc: [", d.code.value_or(std::string{}), "] ", d.message);
        }
    }
    CHECK_FALSE(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B unit literal type mismatch with Int") {
    const auto a = compile_project_loose("unit_mismatch_int",
                                         R"AHFL(
        module b2::unit_mismatch_int;
        fn main() -> Int effect Pure decreases 0 {
            let u: Int = {};
            return 0;
        }
        )AHFL");
    CHECK(a.tc.has_errors());
    CHECK(find_diag_with_code(a.tc.diagnostics, "typecheck.TYPE_MISMATCH") != nullptr);
}

TEST_CASE("P3-gaps-B unit literal in binary op is rejected") {
    const auto a = compile_project_loose("unit_binary",
                                         R"AHFL(
        module b2::unit_binary;
        fn main() -> Int effect Pure decreases 0 {
            let x = {} + 1;
            return 0;
        }
        )AHFL");
    CHECK(a.tc.has_errors());
}

TEST_CASE("P3-gaps-B unit literal as if condition is rejected") {
    const auto a = compile_project_loose("unit_if_cond",
                                         R"AHFL(
        module b2::unit_if_cond;
        fn main() -> Int effect Pure decreases 0 {
            if {} {}
            return 0;
        }
        )AHFL");
    CHECK(a.tc.has_errors());
}

// ============================================================================
// B6: `{}` is const-evaluable
// ============================================================================

TEST_CASE("P3-gaps-B const X: Unit = {} evaluates without errors") {
    const auto a = compile_project_loose("unit_const",
                                         R"AHFL(
        module b2::unit_const;
        const X: Unit = {};
        fn main() -> Int effect Pure decreases 0 {
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());

    // Verify the typed program recorded a Unit const value for `{}`.
    bool found_unit_const = false;
    for (const auto &expr : a.tc.typed_program.expressions) {
        if (expr.const_value.has_value() &&
            expr.const_value->kind == ahfl::ConstValueKind::Unit) {
            found_unit_const = true;
            break;
        }
    }
    CHECK(found_unit_const);
}

// ============================================================================
// Evaluator: unit literal at runtime
// ============================================================================

TEST_CASE("P3-gaps-B evaluator passes unit literal to function") {
    const std::string source = R"AHFL(
fn take_unit(u: Unit) -> Int effect Pure decreases 0 {
    return 1;
}

fn caller() -> Int effect Pure decreases 0 {
    return take_unit({});
}
)AHFL";

    auto compiled = compile_runtime("unit_eval.ahfl", source);
    REQUIRE(compiled.has_value());

    auto result = run_caller(*compiled);
    REQUIRE(result.has_value());

    const auto *int_val = std::get_if<ahfl::evaluator::IntValue>(&result->node);
    REQUIRE(int_val != nullptr);
    CHECK_EQ(int_val->value, 1);
}

// ============================================================================
// Golden: AST printer
// ============================================================================

TEST_CASE("P3-gaps-B AST printer pins unit literal") {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(
        "unit_ast.ahfl",
        R"AHFL(
fn f() -> Unit effect Pure decreases 0 {
    return {};
}
)AHFL");
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    std::ostringstream out;
    ahfl::dump_program_outline(*parse_result.program, out);
    const std::string text = out.str();
    CHECK(text.find("unit") != std::string::npos);
}

// ============================================================================
// Golden: formatter
// ============================================================================

TEST_CASE("P3-gaps-B formatter preserves unit literal") {
    const std::string source = R"AHFL(
fn f() -> Unit effect Pure decreases 0 {
    return {};
}
)AHFL";

    auto result = ahfl::formatter::format_source(source);
    CHECK(result.success);
    CHECK(result.formatted.find("{}") != std::string::npos);
}

// ============================================================================
// Golden: IR printer
// ============================================================================

TEST_CASE("P3-gaps-B IR printer pins unit literal") {
    const std::string source = R"AHFL(
fn f() -> Unit effect Pure decreases 0 {
    return {};
}
)AHFL";

    auto compiled = compile_runtime("unit_ir.ahfl", source);
    REQUIRE(compiled.has_value());

    std::ostringstream out;
    ahfl::print_program_ir(compiled->program_ir, out);
    const std::string text = out.str();
    CHECK(text.find("{}") != std::string::npos);
}

// ============================================================================
// B2: SSA lowering of unit literal produces monostate constant
// ============================================================================

TEST_CASE("P3-gaps-B opt lowering of unit literal produces monostate constant") {
    ahfl::ir::Program program;

    // Build: let u: Unit = {};
    auto unit_expr = program.expr_arena.make(ahfl::ir::UnitLiteralExpr{},
                                              std::nullopt,
                                              unit_type_ref());
    auto stmt = ahfl::make_owned<ahfl::ir::Statement>(ahfl::ir::Statement{
        .node =
            ahfl::ir::LetStatement{
                .name = "u",
                .type_ref = unit_type_ref(),
                .initializer = unit_expr,
            },
        .source_range = {},
    });

    std::vector<ahfl::ir::StatementPtr> stmts;
    stmts.push_back(std::move(stmt));
    add_simple_flow(program, std::move(stmts));

    auto opt = ahfl::ir::opt::lower_to_opt(program);
    REQUIRE_FALSE(opt.functions.empty());

    const auto *func = find_opt_function(opt, "flow::TestAgent::Init");
    REQUIRE(func != nullptr);
    REQUIRE_FALSE(func->blocks.empty());

    // The entry block should have an Assign statement whose rvalue is a Use
    // with a Constant(monostate) operand and Unit result_type.
    const auto &entry_block = func->blocks.front();
    bool found_unit_const = false;
    for (const auto &s : entry_block.statements) {
        if (s.kind != ahfl::ir::opt::Statement::Kind::Assign) continue;
        if (s.rvalue.kind != ahfl::ir::opt::Rvalue::Kind::Use) continue;
        if (s.rvalue.result_type.kind != ahfl::ir::TypeRefKind::Unit) continue;
        for (const auto &operand : s.rvalue.operands) {
            if (operand.kind == ahfl::ir::opt::Operand::Kind::Constant &&
                std::holds_alternative<std::monostate>(operand.constant)) {
                found_unit_const = true;
                break;
            }
        }
        if (found_unit_const) break;
    }
    CHECK(found_unit_const);
}

// ============================================================================
// B3: value_to_json serializes unit as null
// ============================================================================

TEST_CASE("P3-gaps-B value_to_json serializes unit as null") {
    const auto unit = ahfl::evaluator::make_unit();
    const std::string json = ahfl::evaluator::value_to_json(unit);
    CHECK_EQ(json, "null");
}

// ============================================================================
// B3: response_schema_validator accepts UnitValue against Unit schema
// ============================================================================

TEST_CASE("P3-gaps-B response_schema_validator accepts unit value") {
    const auto unit = ahfl::evaluator::make_unit();
    const auto schema = unit_type_ref();
    const auto result = ahfl::runtime::validate_value_against_schema(unit, schema);
    CHECK(result.valid);
}

// ============================================================================
// SMV pin: unit literal does not crash the SMV backend
// ============================================================================

TEST_CASE("P3-gaps-B SMV backend does not crash on unit literal") {
    const std::string source = R"AHFL(
fn f() -> Unit effect Pure decreases 0 {
    return {};
}
)AHFL";

    auto compiled = compile_runtime("unit_smv.ahfl", source);
    REQUIRE(compiled.has_value());

    std::ostringstream out;
    const auto emit_result = ahfl::emit_backend(ahfl::BackendKind::Smv, compiled->program_ir, out);
    CHECK(emit_result.has_value());
    // The SMV output should contain the MODULE header.
    CHECK(out.str().find("MODULE") != std::string::npos);
}

// ============================================================================
// B5: wildcard let lowers to ir::ExprStatement (not ir::LetStatement)
//
// The riskiest new code path: if the HIR->IR lowering branch for
// stmt.is_wildcard were never taken, wildcard lets would silently lower to
// ir::LetStatement with name="_" and no typecheck-level test would catch it.
// This test pins the IR shape directly.
// ============================================================================

TEST_CASE("P3-gaps-B wildcard let lowers to ExprStatement in IR") {
    const std::string source = R"AHFL(
fn caller() -> Int effect Pure decreases 0 {
    let _ = 42;
    return 0;
}
)AHFL";

    auto compiled = compile_runtime("wildcard_ir_shape.ahfl", source);
    REQUIRE(compiled.has_value());

    const ahfl::ir::Block *body = find_fn_body(*compiled, "caller");
    REQUIRE(body != nullptr);
    REQUIRE_GE(body->statements.size(), 2u);

    // The wildcard let MUST lower to ir::ExprStatement — the initializer is
    // evaluated for effects but no binding is recorded.
    const auto &first = body->statements.front();
    REQUIRE(first != nullptr);
    CHECK(std::get_if<ahfl::ir::ExprStatement>(&first->node) != nullptr);
    CHECK(std::get_if<ahfl::ir::LetStatement>(&first->node) == nullptr);

    // The second statement is the return.
    const auto &second = body->statements[1];
    REQUIRE(second != nullptr);
    CHECK(std::get_if<ahfl::ir::ReturnStatement>(&second->node) != nullptr);
}

// ============================================================================
// Combined: wildcard let + unit literal in a single binding
// ============================================================================

TEST_CASE("P3-gaps-B let _: Unit = {} typechecks") {
    const auto a = compile_project_loose("wildcard_unit",
                                         R"AHFL(
        module b2::wildcard_unit;
        fn main() -> Int effect Pure decreases 0 {
            let _: Unit = {};
            return 0;
        }
        )AHFL");
    CHECK_FALSE(a.tc.has_errors());
}

// ============================================================================
// B3: structurally_equal on unit values
// ============================================================================

TEST_CASE("P3-gaps-B structurally_equal on unit values") {
    const auto a = ahfl::evaluator::make_unit();
    const auto b = ahfl::evaluator::make_unit();
    CHECK(ahfl::evaluator::structurally_equal(a, b));
}

// ============================================================================
// B4/B5: wildcard let evaluates the initializer at runtime (effects)
//
// The executor must still evaluate the initializer expression even though the
// result is discarded. This test installs a trace sink and proves the call
// inside the wildcard let's initializer was actually dispatched.
// ============================================================================

TEST_CASE("P3-gaps-B wildcard let evaluates initializer at runtime") {
    const std::string source = R"AHFL(
fn id(x: Int) -> Int effect Pure decreases 0 {
    return x;
}

fn caller() -> Int effect Pure decreases 0 {
    let _ = id(42);
    return 0;
}
)AHFL";

    auto compiled = compile_runtime("wildcard_runtime_effects.ahfl", source);
    REQUIRE(compiled.has_value());

    const ahfl::ir::Block *body = find_fn_body(*compiled, "caller");
    REQUIRE(body != nullptr);

    // Install a trace sink so we can prove the initializer (id(42)) was
    // actually dispatched — the wildcard discards the result but MUST still
    // evaluate the expression for effects.
    ahfl::evaluator::ExecContext ctx;
    std::ostringstream trace_out;
    ahfl::evaluator::RuntimeFnTrace trace_cfg;
    trace_cfg.out = &trace_out;
    compiled->fn_table.install(ctx, trace_cfg);

    const ahfl::evaluator::ExecResult r = ahfl::evaluator::exec_block(*body, ctx);
    REQUIRE_FALSE(r.has_errors());

    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&r.outcome);
    REQUIRE(ret != nullptr);
    const auto *int_val = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
    REQUIRE(int_val != nullptr);
    CHECK_EQ(int_val->value, 0);

    // The trace must record the dispatch to `id` — proving the wildcard let's
    // initializer was evaluated even though its result was discarded.
    const std::string trace = trace_out.str();
    CHECK_FALSE(trace.empty());
    CHECK(trace.find("id") != std::string::npos);
}

// ============================================================================
// Golden: AST printer pins wildcard let
// ============================================================================

TEST_CASE("P3-gaps-B AST printer pins wildcard let") {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(
        "wildcard_let_ast.ahfl",
        R"AHFL(
fn f() -> Int effect Pure decreases 0 {
    let _ = 42;
    return 0;
}
)AHFL");
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    std::ostringstream out;
    ahfl::dump_program_outline(*parse_result.program, out);
    const std::string text = out.str();
    CHECK(text.find("let _") != std::string::npos);
}

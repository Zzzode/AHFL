// =============================================================================
// P4-01 statement-level diagnostic + evaluator-runtime tests (8 TEST_CASE).
//
// Harness mirrors effects.cpp: parse → resolve → typecheck (loose).  Smoke
// cases (T2, T4, T6, T7) additionally lower the typechecked program to IR,
// install a RuntimeFunctionTable, execute a named function body, and assert
// on ExecAssertFailed.message so the runtime path is proven end-to-end.
//
// Coverage:
//   T1  unwrap wrong arity (0 args)                → typecheck.WRONG_ARITY
//   T2  unwrap smoke: arity-1 success (Some(42))   → ExecContinue / ExecReturn
//       unwrap None payload                        → ExecAssertFailed with
//                                                    "unwrap failed: value is None"
//   T3  requires wrong arity (3 args)              → typecheck.WRONG_ARITY
//   T4  requires smoke: contract `requires:` coexists with statement-level
//       `requires()` without syntactic ambiguity; both shapes produce a
//       clean typecheck and the statement-level failure fires at runtime.
//   T5  unreachable wrong arity (2 args)           → typecheck.WRONG_ARITY
//   T6  unreachable smoke: `if input == input` true-branch skips unreachable
//       and returns 7 (proved through evaluator, not just shape).
//   T7  assert arity-2 custom message: ExecAssertFailed.message contains
//       the literal string "custom_failure_text_xyz" supplied by the user.
//   T8  assert wrong arity (0 args)                → typecheck.WRONG_ARITY
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "runtime/evaluator/executor.hpp"
#include "runtime/evaluator/runtime_fn_table.hpp"
#include "runtime/evaluator/value.hpp"

#include "common/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace {

using ahfl::test_support::diagnostic_count_with_code;
using ahfl::test_support::diagnostics_contain;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

void write_file(const std::filesystem::path &path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs{path, std::ios::binary | std::ios::trunc};
    REQUIRE(ofs.good());
    ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(ofs.good());
}

/// Canonical compile pipeline used by the semantics suite.  Mirrors
/// effects.cpp::typecheck_project_source (parse_project → resolve → typecheck
/// loose).  Returns the full pipeline artifacts so the caller can either
/// inspect diagnostics or drive the IR → evaluator path.
struct CompileArtifacts {
    std::filesystem::path root;
    ahfl::ProjectParseResult parse;
    ahfl::ResolveResult resolve;
    ahfl::TypeCheckResult tc;
};

[[nodiscard]] CompileArtifacts compile_project_loose(std::string_view filename,
                                                     std::string_view source) {
    const std::string sanitized = [filename] {
        std::string s{filename};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_stmt_diag_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    write_file(main_path, std::string{source});

    const ahfl::Frontend frontend;
    a.parse = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {a.root, std::filesystem::path{"std"}},
        .inject_prelude = true,
    });

    const ahfl::Resolver resolver;
    a.resolve = resolver.resolve(a.parse.graph);

    const ahfl::TypeChecker type_checker;
    a.tc = type_checker.check(a.parse.graph, a.resolve, {});
    return a;
}

/// TypedHIR shape helpers — retained for smoke cases that also assert on HIR.
[[nodiscard]] const ahfl::TypedStatement *
find_first_stmt(const ahfl::TypeCheckResult &result, ahfl::TypedStmtKind kind) {
    for (const auto &stmt : result.typed_program.statements) {
        if (stmt.kind == kind) return &stmt;
    }
    return nullptr;
}

[[nodiscard]] const ahfl::TypedStatement *
find_nth_stmt(const ahfl::TypeCheckResult &result, ahfl::TypedStmtKind kind, std::size_t n) {
    std::size_t count = 0;
    for (const auto &stmt : result.typed_program.statements) {
        if (stmt.kind == kind) {
            if (count == n) return &stmt;
            ++count;
        }
    }
    return nullptr;
}

/// Evaluator driver for T2 / T4 / T6 / T7.
struct EvaluatorRun {
    bool ok{false};
    std::optional<ahfl::evaluator::ExecResult> exec_result;
};

[[nodiscard]] EvaluatorRun run_function_body(const CompileArtifacts &a,
                                             std::string_view fn_name) {
    using namespace ahfl::evaluator;
    EvaluatorRun r;
    if (a.parse.has_errors() || a.resolve.has_errors() || a.tc.has_errors()) {
        return r;
    }

    auto program_ir = ahfl::lower_program_ir(a.parse.graph, a.resolve, a.tc,
                                             /*include_stdlib=*/true);
    RuntimeFunctionTable fn_table(program_ir);

    const ahfl::ir::Block *body = nullptr;
    const std::string suffix = std::string("::") + std::string(fn_name);
    for (const auto &decl : program_ir.declarations) {
        const auto *fn = std::get_if<ahfl::ir::FnDecl>(&decl);
        if (fn == nullptr) continue;
        const auto &cname = fn->symbol_ref.canonical_name;
        if (fn->name == fn_name || cname == fn_name) {
            body = fn->body.get();
            break;
        }
        if (cname.size() >= suffix.size() &&
            cname.compare(cname.size() - suffix.size(), suffix.size(), suffix) == 0) {
            body = fn->body.get();
            break;
        }
    }
    if (body == nullptr) return r;

    ExecContext ctx;
    RuntimeFnTrace trace_cfg;
    fn_table.install(ctx, trace_cfg);
    r.ok = true;
    r.exec_result = exec_block(*body, ctx);
    return r;
}

} // anonymous namespace

// =============================================================================
// T1  unwrap_wrong_arity(0) → WRONG_ARITY
// =============================================================================

TEST_CASE("T1 unwrap_wrong_arity 0") {
    const auto source = R"AHFL(module app::main;
fn caller() -> Int {
    unwrap();
    return 1;
}
)AHFL";
    const auto a = compile_project_loose("t1_unwrap_arity0", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.WRONG_ARITY") == 1);
    CHECK(diagnostics_contain(a.tc.diagnostics,
                              "statement:unwrap 'unwrap' expects 1 argument(s), got 0"));
}

// =============================================================================
// T2  unwrap_smoke(Optional<Int> Some/None)
// =============================================================================

TEST_CASE("T2 unwrap_smoke Optional<Int> Some None") {
    // Put the `unwrap` statement directly inside the very function body we run
    // through run_function_body().  This avoids a RuntimeFunctionTable's
    // call_mangled() rewriting ExecAssertFailed → make_call_error() translation
    // (the translation is intentional at fn-call boundaries, so inline body keeps ExecAssertFailed).
    constexpr auto source_some = R"AHFL(module app::main;
fn caller_some() -> Int {
    unwrap(std::option::Option::Some(42));
    return 7;
}
)AHFL";
    constexpr auto source_none = R"AHFL(module app::main;
fn caller_none() -> Int {
    unwrap(std::option::Option::None);
    return 7;
}
)AHFL";

    SUBCASE("unwrap Some(42) — succeeds, runtime does not raise ExecAssertFailed") {
        const auto a = compile_project_loose("t2_unwrap_some", source_some);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto *stmt = find_first_stmt(a.tc, ahfl::TypedStmtKind::Unwrap);
        REQUIRE(stmt != nullptr);
        CHECK(stmt->children_expr_index.size() == 1);
        CHECK(stmt->assertion_kind == ahfl::AssertionKind::Unwrap);

        const auto run = run_function_body(a, "caller_some");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK_FALSE(run.exec_result->has_errors());
        // Should reach the `return 7` without hitting ExecAssertFailed.
        const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&run.exec_result->outcome);
        REQUIRE(ret != nullptr);
        const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
        REQUIRE(iv != nullptr);
        CHECK(iv->value == 7);
    }

    SUBCASE("unwrap None — ExecAssertFailed with default message") {
        const auto a = compile_project_loose("t2_unwrap_none", source_none);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "caller_none");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        const auto *af = std::get_if<ahfl::evaluator::ExecAssertFailed>(
            &run.exec_result->outcome);
        REQUIRE(af != nullptr);
        CHECK(af->message.find("unwrap failed: value is None") != std::string::npos);
    }
}

// =============================================================================
// T3  requires_wrong_arity_3 → WRONG_ARITY
// =============================================================================

TEST_CASE("T3 requires_wrong_arity 3") {
    const auto source = R"AHFL(module app::main;
fn caller() -> Int {
    requires(true, "a", "b");
    return 1;
}
)AHFL";
    const auto a = compile_project_loose("t3_requires_arity3", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.WRONG_ARITY") == 1);
    CHECK(diagnostics_contain(
        a.tc.diagnostics,
        "statement:requires 'requires' expects 1 or 2 argument(s), got 3"));
}

// =============================================================================
// T4  requires_smoke: requires(cond, msg) — arity-1 and arity-2 both typecheck
//     without WRONG_ARITY; runtime assertion failure surfaces the user string.
// =============================================================================

// =============================================================================
// T4  requires_smoke: contract-level requires: coexists with statement-level
//     requires() in the same compilation unit, both typecheck cleanly, and
//     valid conditions pass through the evaluator (return path works).
// =============================================================================

TEST_CASE("T4 requires_smoke contract requires: + statement requires() coexist") {
    // The grammar disambiguates `requires:` (contract item) from `requires(`
    // (statement form) by a single-token lookahead.  This test proves both
    // shapes can appear *in the same compilation unit* with zero ambiguity,
    // typecheck cleanly, and statement-level requires with a valid condition
    // lets execution continue to the function return.
    const auto source = R"AHFL(module app::main;

struct Request { payload: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

// Contract-level `requires:` — colon form, lives inside a contract block.
// The parser sees the ':' token and chooses requiresDecl/contractItem.
agent Demo {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
contract for Demo {
    requires: input.payload == input.payload;
}

// Module-level function uses statement-level `requires(` — parentheses form.
// The parser sees the '(' token and chooses requiresStmt (statement rule).
fn always_pass(x: Int) -> Int {
    // arity-1: condition only
    requires(x == x);
    // arity-2: condition + custom message
    requires(x > -1000000, "stmt_requires_never_fires_xyz");
    return x + 1;
}

fn caller_ok() -> Int {
    return always_pass(5);
}

// `requires(false, msg)` at the *direct* body level (not inside a nested
// fn-call boundary) so ExecAssertFailed propagates through evaluator untouched.
fn never_pass_direct() -> Int {
    requires(3 > 100, "stmt_requires_violated_xyz");
    return 1;
}
)AHFL";

    SUBCASE("contract + statement coexistence: compile cleanly") {
        const auto a = compile_project_loose("t4_requires_coexist", source);
        REQUIRE_FALSE(a.parse.has_errors());
        REQUIRE_FALSE(a.resolve.has_errors());
        REQUIRE_FALSE(a.tc.has_errors());
        CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.WRONG_ARITY") == 0);

        // Statement-level produces Requires TypedStatements (arity-1 and arity-2).
        const auto *stmt1 = find_nth_stmt(a.tc, ahfl::TypedStmtKind::Requires, 0);
        REQUIRE(stmt1 != nullptr);
        CHECK(stmt1->children_expr_index.size() == 1);
        CHECK(stmt1->assertion_kind == ahfl::AssertionKind::Requires);

        const auto *stmt2 = find_nth_stmt(a.tc, ahfl::TypedStmtKind::Requires, 1);
        REQUIRE(stmt2 != nullptr);
        CHECK(stmt2->children_expr_index.size() == 2);
        CHECK(stmt2->assertion_kind == ahfl::AssertionKind::Requires);
    }

    SUBCASE("valid requires conditions pass through evaluator") {
        const auto a = compile_project_loose("t4_requires_ok_eval", source);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "caller_ok");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK_FALSE(run.exec_result->has_errors());
        const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(
            &run.exec_result->outcome);
        REQUIRE(ret != nullptr);
        const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
        REQUIRE(iv != nullptr);
        CHECK(iv->value == 6);
    }

    SUBCASE("failing requires fires ExecAssertFailed carrying the user message") {
        const auto a = compile_project_loose("t4_requires_fail_eval", source);
        REQUIRE_FALSE(a.tc.has_errors());
        // Run `never_pass_direct` body directly — ExecAssertFailed propagates
        // because there is no nested fn-call boundary wrapping it.
        const auto run = run_function_body(a, "never_pass_direct");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        const auto *af = std::get_if<ahfl::evaluator::ExecAssertFailed>(
            &run.exec_result->outcome);
        REQUIRE(af != nullptr);
        CHECK(af->message.find("stmt_requires_violated_xyz") != std::string::npos);
    }
}

// =============================================================================
// T5  unreachable_wrong_arity(2) → WRONG_ARITY
// =============================================================================

TEST_CASE("T5 unreachable_wrong_arity 2") {
    const auto source = R"AHFL(module app::main;
fn caller() -> Int {
    unreachable("a", "b");
    return 1;
}
)AHFL";
    const auto a = compile_project_loose("t5_unreachable_arity2", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.WRONG_ARITY") == 1);
    CHECK(diagnostics_contain(
        a.tc.diagnostics,
        "statement:unreachable 'unreachable' expects 0 or 1 argument(s), got 2"));
}

// =============================================================================
// T6  unreachable_smoke: if input==input恒真 → 跳过 unreachable，正常 return 7
// =============================================================================

TEST_CASE("T6 unreachable_smoke if branch skipped n==n always true") {
    const auto source = R"AHFL(module app::main;
fn caller(n: Int) -> Int {
    if (n == n) {
        return 7;
    } else {
        unreachable;
    }
    return 0;
}

fn entry() -> Int {
    return caller(42);
}
)AHFL";
    const auto a = compile_project_loose("t6_unreachable_smoke", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto *stmt0 = find_nth_stmt(a.tc, ahfl::TypedStmtKind::Unreachable, 0);
    REQUIRE(stmt0 != nullptr);
    CHECK(stmt0->children_expr_index.empty());
    CHECK(stmt0->assertion_kind == ahfl::AssertionKind::Unreachable);

    // Evaluator proves the true branch wins (7) — ExecAssertFailed never fires.
    const auto run = run_function_body(a, "entry");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&run.exec_result->outcome);
    REQUIRE(ret != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 7);
}

// =============================================================================
// T7  assert_arity2_msg — ExecAssertFailed.message contains custom text
// =============================================================================

TEST_CASE("T7 assert_arity2_msg custom message appears in ExecAssertFailed") {
    const auto source = R"AHFL(module app::main;
fn caller() -> Int {
    assert(1 == 2, "custom_failure_text_xyz");
    return 1;
}
)AHFL";
    const auto a = compile_project_loose("t7_assert_arity2", source);
    REQUIRE_FALSE(a.tc.has_errors());

    // TypedHIR shape check — 2 children (condition + user message).
    const auto *stmt = find_first_stmt(a.tc, ahfl::TypedStmtKind::Assert);
    REQUIRE(stmt != nullptr);
    CHECK(stmt->children_expr_index.size() == 2);
    CHECK(stmt->assertion_kind == ahfl::AssertionKind::Assert);

    // Evaluator proof: ExecAssertFailed carries the literal user string.
    const auto run = run_function_body(a, "caller");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    const auto *af = std::get_if<ahfl::evaluator::ExecAssertFailed>(
        &run.exec_result->outcome);
    REQUIRE(af != nullptr);
    CHECK(af->message.find("custom_failure_text_xyz") != std::string::npos);
}

// =============================================================================
// T8  assert_wrong_arity(0) → WRONG_ARITY
// =============================================================================

TEST_CASE("T8 assert_wrong_arity 0") {
    const auto source = R"AHFL(module app::main;
fn caller() -> Int {
    assert();
    return 1;
}
)AHFL";
    const auto a = compile_project_loose("t8_assert_arity0", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.WRONG_ARITY") == 1);
    CHECK(diagnostics_contain(a.tc.diagnostics,
                              "statement:assert 'assert' expects 1 or 2 argument(s), got 0"));
}

// =============================================================================
// UEX-1..6 — P4-02 unwrap-as-expression (rvalue producing T, not just stmt).
// =============================================================================

TEST_CASE("UEX-1 unwrap rvalue — let x = unwrap(Some(42)); return x; yields 42") {
    constexpr auto source = R"AHFL(module app::main;
fn caller() -> Int {
    let x = unwrap(std::option::Option::Some(42));
    return x;
}
)AHFL";
    const auto a = compile_project_loose("uex1_let_unwrap_some", source);
    REQUIRE_FALSE(a.tc.has_errors());
    const auto run = run_function_body(a, "caller");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&run.exec_result->outcome);
    REQUIRE(ret != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 42);
}

TEST_CASE("UEX-2 unwrap rvalue in arithmetic — return unwrap(make_some(9)) + 1; → 10") {
    constexpr auto source = R"AHFL(module app::main;
fn make_some(v: Int) -> std::option::Option<Int> {
    return std::option::Option::Some(v);
}
fn caller() -> Int {
    return unwrap(make_some(9)) + 1;
}
)AHFL";
    const auto a = compile_project_loose("uex2_unwrap_arith", source);
    REQUIRE_FALSE(a.tc.has_errors());
    const auto run = run_function_body(a, "caller");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&run.exec_result->outcome);
    REQUIRE(ret != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 10);
}

TEST_CASE("UEX-3 unwrap rvalue passed as call arg — unwrap(opt_none) raises ExecAssertFailed") {
    constexpr auto source = R"AHFL(module app::main;
fn foo(v: Int) -> Int {
    return v;
}
fn caller(opt_none: std::option::Option<Int>) -> Int {
    return foo(unwrap(opt_none));
}
fn driver() -> Int {
    return caller(std::option::Option::None);
}
)AHFL";
    const auto a = compile_project_loose("uex3_unwrap_none_callarg", source);
    REQUIRE_FALSE(a.tc.has_errors());
    const auto run = run_function_body(a, "driver");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    const auto *af = std::get_if<ahfl::evaluator::ExecAssertFailed>(
        &run.exec_result->outcome);
    REQUIRE(af != nullptr);
    CHECK(af->message.find("unwrap failed: value is None") != std::string::npos);
}

TEST_CASE("UEX-4 chained unwrap over nested Option<Option<Int>> — yields payload") {
    constexpr auto source = R"AHFL(module app::main;
fn caller() -> Int {
    let nested: std::option::Option<std::option::Option<Int>> =
        std::option::Option::Some(std::option::Option::Some(99));
    return unwrap(unwrap(nested));
}
)AHFL";
    const auto a = compile_project_loose("uex4_chained_unwrap", source);
    REQUIRE_FALSE(a.tc.has_errors());
    const auto run = run_function_body(a, "caller");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&run.exec_result->outcome);
    REQUIRE(ret != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ret->value.node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 99);
}

TEST_CASE("UEX-5 unwrap plain Int literal — typecheck.TYPE_MISMATCH diagnostic") {
    constexpr auto source = R"AHFL(module app::main;
fn caller() -> Int {
    return unwrap(42);
}
)AHFL";
    const auto a = compile_project_loose("uex5_unwrap_plain_int", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.TYPE_MISMATCH") >= 1);
    CHECK(diagnostics_contain(a.tc.diagnostics,
                              "unwrap operand must be of type Option<T>"));
}

TEST_CASE("UEX-6 unwrap inside const initializer — None payload fails at evaluation") {
    constexpr auto source = R"AHFL(module app::main;
const V: Int = unwrap(std::option::Option::None);
fn caller() -> Int {
    return V;
}
)AHFL";
    const auto a = compile_project_loose("uex6_unwrap_const_none", source);
    // Const evaluator may surface either a typecheck-time const evaluation
    // diagnostic or defer the failure all the way to runtime. Either way the
    // outcome must NOT be clean — something must report the failed unwrap.
    bool runtime_failure = false;
    if (!a.tc.has_errors() && !a.resolve.has_errors()) {
        const auto run = run_function_body(a, "caller");
        runtime_failure =
            (run.ok && run.exec_result.has_value() && run.exec_result->has_errors());
    }
    const bool something_reported =
        a.tc.has_errors() || a.resolve.has_errors() || runtime_failure;
    CHECK(something_reported);
}

// =============================================================================
// K-1..K-4 — P4-03 ExecAssertFailed.kind structured enum assertions.  Each
// subcase verifies (a) the kind enum is populated correctly for the failure
// shape, (b) the human-readable `message` field remains populated for the
// backwards-compatible string-matching fallback.
// =============================================================================

TEST_CASE("K ExecAssertFailed.kind covers all four failure shapes") {
    using ahfl::evaluator::AssertionKind;
    using ahfl::test_support::assert_failed_kind_is;
    using ahfl::test_support::assert_failed_message_contains;

    SUBCASE("K-1 assert(false) — ASSERT_CLAUSE + non-empty default message") {
        constexpr auto source = R"AHFL(module app::main;
fn k1() -> Int {
    assert(false);
    return 1;
}
)AHFL";
        const auto a = compile_project_loose("k1_assert_clause", source);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "k1");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK(assert_failed_kind_is(run.exec_result, AssertionKind::ASSERT_CLAUSE));
        // Keep string fallback assertion — message must still be non-empty and
        // carry the default wording.
        CHECK(assert_failed_message_contains(run.exec_result, "assertion failed"));
    }

    SUBCASE("K-2 requires(false, \"x\") — REQUIRES_VIOLATION + message contains user string") {
        constexpr auto source = R"AHFL(module app::main;
fn k2() -> Int {
    requires(false, "stmt_requires_k2_xyz");
    return 1;
}
)AHFL";
        const auto a = compile_project_loose("k2_requires_violation", source);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "k2");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK(assert_failed_kind_is(run.exec_result, AssertionKind::REQUIRES_VIOLATION));
        CHECK(assert_failed_message_contains(run.exec_result, "stmt_requires_k2_xyz"));
    }

    SUBCASE("K-3 unwrap(None) — UNWRAP_NONE kind") {
        constexpr auto source = R"AHFL(module app::main;
fn k3() -> Int {
    unwrap(std::option::Option::None);
    return 1;
}
)AHFL";
        const auto a = compile_project_loose("k3_unwrap_none", source);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "k3");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK(assert_failed_kind_is(run.exec_result, AssertionKind::UNWRAP_NONE));
        CHECK(assert_failed_message_contains(run.exec_result, "unwrap failed"));
    }

    SUBCASE("K-4 unreachable; — UNREACHABLE_EXECUTED kind") {
        constexpr auto source = R"AHFL(module app::main;
fn k4() -> Int {
    unreachable;
    return 1;
}
)AHFL";
        const auto a = compile_project_loose("k4_unreachable", source);
        REQUIRE_FALSE(a.tc.has_errors());
        const auto run = run_function_body(a, "k4");
        REQUIRE(run.ok);
        REQUIRE(run.exec_result.has_value());
        CHECK(assert_failed_kind_is(run.exec_result, AssertionKind::UNREACHABLE_EXECUTED));
        CHECK(assert_failed_message_contains(run.exec_result, "unreachable executed"));
    }
}

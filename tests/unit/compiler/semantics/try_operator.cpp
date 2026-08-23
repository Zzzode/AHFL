// =============================================================================
// RFC 0014: try operator (`expr?`) for Option<T> and Result<T, E>.
//
// Slice 1: `?` is only allowed as the direct initializer of a let binding
// (`let x = e?;`). The HIR->IR lowerer expands it at the statement level:
//
//   let x = e?;
//
// lowers to:
//
//   let <tmp> = e;                          // evaluate the operand
//   if let <Pat> = <tmp> { return <Fail>; } // failure branch (None / Err)
//   let x = unwrap(<tmp>);                  // success path
//
// This file covers:
//   - Option<T> smoke: Some path yields T, None path returns None early.
//   - Result<T, E> smoke: Ok path yields T, Err path returns Err early.
//   - Negative diagnostics: non-Option/Result operand, incompatible enclosing
//     return type, `?` outside let-binding position.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/evaluator/executor.hpp"
#include "runtime/evaluator/runtime_fn_table.hpp"
#include "runtime/evaluator/value.hpp"

#include "common/project_input_support.hpp"
#include "common/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ahfl::test_support::diagnostic_count_with_code;

// ---------------------------------------------------------------------------
// Shared helpers (mirrors stmt_diagnostics.cpp harness)
// ---------------------------------------------------------------------------

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

[[nodiscard]] CompileArtifacts compile_project_loose(std::string_view filename,
                                                     std::string_view source) {
    const std::string sanitized = [filename] {
        std::string s{filename};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_try_op_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    std::string project_source{source};
    constexpr std::string_view module_decl = "module app::main;\n";
    if (project_source.starts_with(module_decl)) {
        if (project_source.find("import std::option;") == std::string::npos) {
            project_source.insert(module_decl.size(), "import std::option;\n");
        }
        if (project_source.find("import std::result;") == std::string::npos) {
            project_source.insert(module_decl.size(), "import std::result;\n");
        }
    }
    write_file(main_path, project_source);

    const ahfl::Frontend frontend;
    a.parse = ahfl::parse_project(
        frontend,
        ahfl::test_support::project_input_with_repo_std_for_test_file(main_path, a.root, __FILE__));

    const ahfl::Resolver resolver;
    a.resolve = resolver.resolve(a.parse.graph);

    const ahfl::TypeChecker type_checker;
    a.tc = type_checker.check(a.parse.graph, a.resolve, {});
    return a;
}

struct EvaluatorRun {
    bool ok{false};
    std::optional<ahfl::evaluator::ExecResult> exec_result;
};

[[nodiscard]] EvaluatorRun run_function_body(const CompileArtifacts &a, std::string_view fn_name) {
    using namespace ahfl::evaluator;
    EvaluatorRun r;
    if (a.parse.has_errors() || a.resolve.has_errors() || a.tc.has_errors()) {
        return r;
    }

    auto program_ir = ahfl::lower_program_ir(a.parse.graph,
                                             a.resolve,
                                             a.tc,
                                             /*include_stdlib=*/true);
    RuntimeFunctionTable fn_table(program_ir);

    const ahfl::ir::Block *body = nullptr;
    const std::string suffix = std::string("::") + std::string(fn_name);
    for (const auto &decl : program_ir.declarations) {
        const auto *fn = std::get_if<ahfl::ir::FnDecl>(&decl);
        if (fn == nullptr)
            continue;
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
    if (body == nullptr)
        return r;

    ExecContext ctx;
    RuntimeFnTrace trace_cfg;
    fn_table.install(ctx, trace_cfg);
    r.ok = true;
    r.exec_result = exec_block(*body, ctx);
    return r;
}

/// Extract the EnumValue from an ExecReturn, or nullptr if the outcome is
/// not a return of an enum value.
[[nodiscard]] const ahfl::evaluator::EnumValue *
expect_enum_return(const ahfl::evaluator::ExecResult &result) {
    const auto *ret = std::get_if<ahfl::evaluator::ExecReturn>(&result.outcome);
    if (ret == nullptr)
        return nullptr;
    return std::get_if<ahfl::evaluator::EnumValue>(&ret->value.node);
}

} // anonymous namespace

// =============================================================================
// Option<T> smoke tests
// =============================================================================

TEST_CASE("try_option_some yields T") {
    constexpr auto source = R"AHFL(module app::main;
fn try_some() -> Option<Int> {
    let x = std::option::Option::Some(42)?;
    return std::option::Option::Some(x + 1);
}
)AHFL";
    const auto a = compile_project_loose("try_option_some", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto run = run_function_body(a, "try_some");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());

    const auto *ev = expect_enum_return(*run.exec_result);
    REQUIRE(ev != nullptr);
    CHECK(ev->enum_name == "std::option::Option");
    CHECK(ev->variant == "Some");
    REQUIRE(ev->payload.size() == 1);
    REQUIRE(ev->payload.front() != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ev->payload.front()->node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 43);
}

TEST_CASE("try_option_none returns None early") {
    constexpr auto source = R"AHFL(module app::main;
fn try_none() -> Option<Int> {
    let x = std::option::Option::None?;
    return std::option::Option::Some(x + 1);
}
)AHFL";
    const auto a = compile_project_loose("try_option_none", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto run = run_function_body(a, "try_none");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());

    const auto *ev = expect_enum_return(*run.exec_result);
    REQUIRE(ev != nullptr);
    CHECK(ev->enum_name == "std::option::Option");
    CHECK(ev->variant == "None");
    CHECK(ev->payload.empty());
}

// =============================================================================
// Result<T, E> smoke tests
// =============================================================================

TEST_CASE("try_result_ok yields T") {
    constexpr auto source = R"AHFL(module app::main;
fn try_ok() -> Result<Int, String> {
    let x = std::result::Result::Ok(42)?;
    return std::result::Result::Ok(x + 1);
}
)AHFL";
    const auto a = compile_project_loose("try_result_ok", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto run = run_function_body(a, "try_ok");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());

    const auto *ev = expect_enum_return(*run.exec_result);
    REQUIRE(ev != nullptr);
    CHECK(ev->enum_name == "std::result::Result");
    CHECK(ev->variant == "Ok");
    REQUIRE(ev->payload.size() == 1);
    REQUIRE(ev->payload.front() != nullptr);
    const auto *iv = std::get_if<ahfl::evaluator::IntValue>(&ev->payload.front()->node);
    REQUIRE(iv != nullptr);
    CHECK(iv->value == 43);
}

TEST_CASE("try_result_err returns Err early") {
    constexpr auto source = R"AHFL(module app::main;
fn try_err() -> Result<Int, String> {
    let x = std::result::Result::Err("boom")?;
    return std::result::Result::Ok(x + 1);
}
)AHFL";
    const auto a = compile_project_loose("try_result_err", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto run = run_function_body(a, "try_err");
    REQUIRE(run.ok);
    REQUIRE(run.exec_result.has_value());
    CHECK_FALSE(run.exec_result->has_errors());

    const auto *ev = expect_enum_return(*run.exec_result);
    REQUIRE(ev != nullptr);
    CHECK(ev->enum_name == "std::result::Result");
    CHECK(ev->variant == "Err");
    REQUIRE(ev->payload.size() == 1);
    REQUIRE(ev->payload.front() != nullptr);
    const auto *sv = std::get_if<ahfl::evaluator::StringValue>(&ev->payload.front()->node);
    REQUIRE(sv != nullptr);
    CHECK(sv->value == "boom");
}

// =============================================================================
// Negative diagnostics
// =============================================================================

TEST_CASE("try_on_non_option_result_operand") {
    constexpr auto source = R"AHFL(module app::main;
fn try_int() -> Option<Int> {
    let x = 42?;
    return std::option::Option::Some(x);
}
)AHFL";
    const auto a = compile_project_loose("try_non_option", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_REQUIRES_OPTION_OR_RESULT") == 1);
}

TEST_CASE("try_incompatible_return_type") {
    // `?` on Option<T> requires the enclosing fn to return Option<_>.
    // Here the fn returns Int, which is incompatible.
    constexpr auto source = R"AHFL(module app::main;
fn try_bad_return() -> Int {
    let x = std::option::Option::Some(42)?;
    return x;
}
)AHFL";
    const auto a = compile_project_loose("try_incompatible", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_INCOMPATIBLE_RETURN_TYPE") == 1);
}

TEST_CASE("try_not_in_let_binding_position") {
    // Slice 1: `?` is only allowed as the direct initializer of a let.
    // Here it appears in a return expression.
    constexpr auto source = R"AHFL(module app::main;
fn try_return() -> Option<Int> {
    return std::option::Option::Some(42)?;
}
)AHFL";
    const auto a = compile_project_loose("try_not_let", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_NOT_IN_LET_BINDING") == 1);
}

TEST_CASE("try_nested_in_call_not_let_binding") {
    // Slice 1: `?` inside a call argument is not in let-binding position.
    constexpr auto source = R"AHFL(module app::main;
fn try_nested(o: Option<Int>) -> Option<Int> {
    let x = std::option::Option::Some(o?);
    return x;
}
)AHFL";
    const auto a = compile_project_loose("try_nested", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_NOT_IN_LET_BINDING") == 1);
}

// =============================================================================
// Forbidden context: flow handler (TRY_OUTSIDE_FUNCTION)
// =============================================================================

TEST_CASE("try_in_flow_handler_rejected") {
    // `?` is forbidden in flow handler state bodies — there is no enclosing
    // fn return type to early-return into.
    constexpr auto source = R"AHFL(module app::main;
struct TryReq { v: Int; }
struct TryCtx { sum: Int = 0; }
struct TryRes { sum: Int; }
agent TryAgent {
    input: TryReq;
    context: TryCtx;
    output: TryRes;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    transition Init -> Done;
}
flow for TryAgent {
    state Init {
        let x = std::option::Option::Some(input.v)?;
        ctx.sum = x;
        goto Done;
    }
    state Done {
        return TryRes { sum: ctx.sum };
    }
}
)AHFL";
    const auto a = compile_project_loose("try_flow_handler", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_OUTSIDE_FUNCTION") == 1);
}

// =============================================================================
// Closure tests
// =============================================================================

// NOTE: TRY_IN_CLOSURE_WITHOUT_RETURN_TYPE is implemented in visit_try_expr
// but cannot be exercised in Slice 1: AHFL lambda bodies are single
// expressions (`\x -> expr`), not blocks with let bindings. A `?` inside a
// lambda body is therefore rejected by TRY_NOT_IN_LET_BINDING first (the
// try_allowed gate fires before the closure gate). The closure check remains
// as a safety net for a future slice that permits `?` in arbitrary expression
// positions (A-Normalization), where closures without a concretely
// determinable return type must reject `?` (Rust-consistent).

// =============================================================================
// Regression: adversarial-verifier blocking fixes
// =============================================================================

TEST_CASE("try_wildcard_let_rejected") {
    // BLOCKING #1: `let _ = e?;` was accepted by sema but crashed the IR
    // lowerer (wildcard lets are excluded from the try-let expansion). Now
    // rejected at sema: try_allowed is false for wildcard lets.
    constexpr auto source = R"AHFL(module app::main;
fn try_wildcard(o: Option<Int>) -> Option<Int> {
    let _ = o?;
    return std::option::Option::Some(0);
}
)AHFL";
    const auto a = compile_project_loose("try_wildcard", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_NOT_IN_LET_BINDING") == 1);
}

TEST_CASE("try_fn_without_return_type_rejected") {
    // BLOCKING #2: `?` in a fn without a declared return type was silently
    // accepted (error-typed return skipped all checks). Now rejected with
    // TRY_OUTSIDE_FUNCTION: the return signature is not determinable.
    constexpr auto source = R"AHFL(module app::main;
fn try_no_ret(o: Option<Int>) {
    let x = o?;
}
)AHFL";
    const auto a = compile_project_loose("try_no_ret", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_OUTSIDE_FUNCTION") == 1);
}

TEST_CASE("try_generic_bare_type_param_rejected") {
    // BLOCKING #3: `?` in a generic fn returning a bare type parameter was
    // silently accepted (contains_type_var guard skipped the entire shape
    // check). Now the Option/Result shape check runs unconditionally: bare T
    // is neither Option nor Result, so TRY_INCOMPATIBLE_RETURN_TYPE fires.
    constexpr auto source = R"AHFL(module app::main;
fn try_bare_t<T>(o: Option<T>) -> T {
    let x = o?;
    return x;
}
)AHFL";
    const auto a = compile_project_loose("try_bare_t", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.TRY_INCOMPATIBLE_RETURN_TYPE") == 1);
}

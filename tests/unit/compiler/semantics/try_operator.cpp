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
//   - Negative diagnostics: non-Option/Result operand, incompatible enclosing
//     return type, `?` outside let-binding position.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "base/json/json_value.hpp"
#include "runtime/value/value.hpp"

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

void verify_lowered(const ahfl::TypedProgram &typed, const ahfl::SourceGraph &graph,
                    bool verify_execution) {
    const auto ir = ahfl::lower_typed_program(typed, graph);
    const auto verified =
        ahfl::ir::verify_ir_program(ir, ahfl::ir::IrVerificationMode::BackendReady);
    for (const auto &diagnostic : verified.diagnostics) {
        INFO(diagnostic.path << ": " << diagnostic.message);
        CHECK(diagnostic.severity != ahfl::ir::VerificationSeverity::Error);
    }
    CHECK_FALSE(verified.has_errors());
    if (!verify_execution) {
        // This library still contains abstract generic declarations. Core
        // requires executable-closure selection before lowering those bodies;
        // the CLI regression exercises their concrete Int/Bool instantiations.
        return;
    }
    const auto core = ahfl::ir::core::lower_ahfl_to_core(ir);
    for (const auto &diagnostic : core.diagnostics) {
        INFO(diagnostic.message);
        CHECK(diagnostic.severity != ahfl::ir::core::CoreDiagnosticSeverity::Error);
    }
    CHECK_FALSE(core.has_errors());
}

void verify_try_program(std::string_view filename, std::string_view source,
                        bool verify_execution = true) {
    const auto a = compile_project_loose(filename, source);
    REQUIRE_FALSE(a.parse.has_errors());
    REQUIRE_FALSE(a.resolve.has_errors());
    for (const auto &diagnostic : a.tc.diagnostics.entries()) {
        INFO(diagnostic.message);
        CHECK(diagnostic.severity != ahfl::DiagnosticSeverity::Error);
    }
    REQUIRE_FALSE(a.tc.has_errors());
    verify_lowered(a.tc.typed_program, a.parse.graph, verify_execution);
    // Persistent Typed HIR must retain the same propagation facts as a fresh
    // typecheck, including source and target types in coercion witnesses.
    const auto restored = ahfl::deserialize_typed_program_json(
        ahfl::serialize_typed_program_json(a.tc.typed_program));
    REQUIRE(restored.has_value());
    verify_lowered(*restored, a.parse.graph, verify_execution);
}

} // anonymous namespace

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

// Success and failure paths must survive the production verifier, not merely
// typechecking: every generated path, pattern and constructor is fully typed.
TEST_CASE("try_option_and_result_pass_backend_ready_and_core_verifiers") {
    verify_try_program("try_verified", R"AHFL(module app::main;
fn option_value(value: Option<Int>) -> Option<String> effect Pure decreases 0 {
    let number = value?;
    return std::option::Option::Some("present");
}
fn result_value(value: Result<Int, String>) -> Result<String, String> effect Pure decreases 0 {
    let number = value?;
    return std::result::Result::Ok("present");
}
fn option_constructor_to_text() -> Option<String> effect Pure decreases 0 {
    let number: Int = (std::option::Option::Some(7))?;
    return std::option::Option::Some("present");
}
fn result_constructor_to_text() -> Result<String, String> effect Pure decreases 0 {
    let number: Int = std::result::Result::Ok(7)?;
    return std::result::Result::Ok("present");
}
fn option_constructors() -> Option<Int> effect Pure decreases 0 {
    let first: Int = std::option::Option::Some(7)?;
    let second: Int = std::option::Option::None?;
    return std::option::Option::Some(first + second);
}
fn result_constructors() -> Result<Int, String> effect Pure decreases 0 {
    let first: Int = std::result::Result::Ok(7)?;
    let second: Int = std::result::Result::Err("stop")?;
    return std::result::Result::Ok(first + second);
}
)AHFL");
}

TEST_CASE("try_preserves_success_and_failure_adjustment_witnesses") {
    verify_try_program("try_adjustments", R"AHFL(module app::main;
fn widen_success(value: Option<Int(0, 10)>) -> Option<Int> effect Pure decreases 0 {
    let number: Int = value?;
    return std::option::Option::Some(number);
}
fn widen_error_constructor(error: Int(0, 10)) -> Result<String, Int> effect Pure decreases 0 {
    let number: Int = std::result::Result::Err(error)?;
    return std::result::Result::Ok("present");
}
fn widen_error(value: Result<Int, Int(0, 10)>) -> Result<String, Int> effect Pure decreases 0 {
    let number = value?;
    return std::result::Result::Ok("present");
}
)AHFL");
}

TEST_CASE("try_generic_instantiation_and_user_names_pass_verifiers") {
    verify_try_program("try_generic_verified", R"AHFL(module app::main;
fn generic_option<T>(value: std::option::Option<T>) -> std::option::Option<T> effect Pure decreases 0 {
    let item: T = value?;
    return std::option::Option::Some(item);
}
fn generic_result<T, E>(value: std::result::Result<T, E>) -> std::result::Result<T, E> effect Pure decreases 0 {
    let item: T = value?;
    return std::result::Result::Ok(item);
}
fn concrete() -> std::option::Option<Int> effect Pure decreases 0 {
    return generic_option<Int>(std::option::Option::Some(7));
}
fn concrete_result() -> std::result::Result<Int, String> effect Pure decreases 0 {
    return generic_result<Int, String>(std::result::Result::Ok(7));
}
fn names(value: std::result::Result<Int, String>, try_tmp_0: Int, err: Int) -> std::result::Result<Int, String> effect Pure decreases 0 {
    let number = value?;
    return std::result::Result::Ok(number + try_tmp_0 + err);
}
)AHFL", false);
}

TEST_CASE("try_rejects_incompatible_result_error_type") {
    const auto a = compile_project_loose("try_incompatible_error", R"AHFL(module app::main;
fn reject_error(value: Result<Int, String>) -> Result<Int, Int> effect Pure decreases 0 {
    let number = value?;
    return std::result::Result::Ok(number);
}
)AHFL");
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.TRY_INCOMPATIBLE_RETURN_TYPE") ==
          1);
}

TEST_CASE("try_rejects_unconstrained_generic_error_conversion") {
    const auto a = compile_project_loose("try_generic_error", R"AHFL(module app::main;
fn incompatible<T, E, F>(value: Result<T, E>) -> Result<T, F> effect Pure decreases 0 {
    let item: T = value?;
    return std::result::Result::Ok(item);
}
)AHFL");
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.TRY_INCOMPATIBLE_RETURN_TYPE") ==
          1);
}

TEST_CASE("try_cache_rejects_missing_or_null_propagation_facts") {
    const auto a = compile_project_loose("try_cache_facts", R"AHFL(module app::main;
fn cache_value(value: Option<Int>) -> Option<Int> effect Pure decreases 0 {
    let item = value?;
    return std::option::Option::Some(item);
}
)AHFL");
    REQUIRE_FALSE(a.tc.has_errors());
    const auto serialized = ahfl::serialize_typed_program_json(a.tc.typed_program);
    auto json = ahfl::json::parse_json(serialized);
    REQUIRE(json.has_value());
    auto *expressions = (*json)->get_mut("expressions");
    REQUIRE(expressions != nullptr);
    auto entry = std::find_if(
        expressions->array_items.begin(), expressions->array_items.end(), [](const auto &expr) {
            const auto *info = expr->get("try_info");
            return info != nullptr && info->is_object();
        });
    REQUIRE(entry != expressions->array_items.end());
    (*entry)->set("try_info", ahfl::json::JsonValue::make_null());
    CHECK_FALSE(
        ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**json)).has_value());
    auto &fields = (*entry)->object_fields;
    fields.erase(std::remove_if(fields.begin(),
                                fields.end(),
                                [](const auto &field) { return field.first == "try_info"; }),
                 fields.end());
    CHECK_FALSE(
        ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**json)).has_value());
}

// =============================================================================
// P4-01 statement-level diagnostic tests (5 TEST_CASE).
//
// Harness: parse → resolve → typecheck (loose).
//
// Coverage:
//   T1  unwrap wrong arity (0 args)                → typecheck.WRONG_ARITY
//   T3  requires wrong arity (3 args)              → typecheck.WRONG_ARITY
//   T5  unreachable wrong arity (2 args)           → typecheck.WRONG_ARITY
//   T8  assert wrong arity (0 args)                → typecheck.WRONG_ARITY
//   UEX-5 unwrap plain Int literal                 → typecheck.TYPE_MISMATCH
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "compiler/syntax/frontend/project.hpp"
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
/// loose).  Returns the full pipeline artifacts so the caller can inspect
/// diagnostics.
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
    std::string project_source{source};
    constexpr std::string_view module_decl = "module app::main;\n";
    if (project_source.starts_with(module_decl) &&
        project_source.find("import std::option;") == std::string::npos) {
        project_source.insert(module_decl.size(), "import std::option;\n");
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
    CHECK(diagnostics_contain(a.tc.diagnostics,
                              "statement:requires 'requires' expects 1 or 2 argument(s), got 3"));
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
        a.tc.diagnostics, "statement:unreachable 'unreachable' expects 0 or 1 argument(s), got 2"));
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

TEST_CASE("UEX-5 unwrap plain Int literal — typecheck.TYPE_MISMATCH diagnostic") {
    constexpr auto source = R"AHFL(module app::main;
fn caller() -> Int {
    return unwrap(42);
}
)AHFL";
    const auto a = compile_project_loose("uex5_unwrap_plain_int", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics, "typecheck.TYPE_MISMATCH") >= 1);
    CHECK(diagnostics_contain(a.tc.diagnostics, "unwrap operand must be of type Option<T>"));
}

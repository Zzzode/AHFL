// =============================================================================
// RFC 0024: bounded collection quantification (forall/exists over List/Set/Map).
//
// This slice covers the front half of the pipeline: parse -> typecheck -> IR
// lowering of `forall x in coll: body` and `exists (k, v) in coll: body`.
//
// Type checking establishes:
//   - the operand after `in` must be a nominal stdlib collection
//     (List<T> / Set<T> / Map<K, V>), else typecheck.QUANTIFIER_REQUIRES_COLLECTION
//   - the binder(s) bind at the element type (List/Set) or the (key, value)
//     pair (Map) in a child scope that shadows outer bindings
//   - the body must have type Bool, else typecheck.QUANTIFIER_BODY_REQUIRES_BOOL
//
// The quantifier lowers to ir::QuantifierExpr (verification-only; the SMT-BMC
// backend unrolls it — see later RFC 0024 slices). It never lowers to
// executable IR, so these tests assert on typecheck outcomes and the presence
// of the lowered ir::QuantifierExpr node in a contract clause.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include "common/project_input_support.hpp"
#include "common/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>

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

// Compiles a single-module project that imports std::collections so the
// nominal List/Set/Map types resolve through the repo sysroot.
[[nodiscard]] CompileArtifacts compile_project(std::string_view test_name, std::string_view source) {
    const std::string sanitized = [test_name] {
        std::string s{test_name};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_quant_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    write_file(main_path, source);

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

// Walks the lowered IR program and returns true if any expression is an
// ir::QuantifierExpr with the given forall/exists kind.
[[nodiscard]] bool ir_has_quantifier(const ahfl::ir::Program &program,
                                     ahfl::ir::QuantifierExpr::Kind kind) {
    for (const auto *expr : program.all_exprs()) {
        if (expr == nullptr) {
            continue;
        }
        if (const auto *quantifier = std::get_if<ahfl::ir::QuantifierExpr>(&expr->node);
            quantifier != nullptr && quantifier->kind == kind) {
            return true;
        }
    }
    return false;
}

// A minimal agent whose input struct carries a collection field, plus a
// contract whose `requires` clause is the quantifier under test. Contract
// clauses are lowered to IR (unlike executable-only fn bodies) so we can
// inspect the ir::QuantifierExpr.
[[nodiscard]] std::string agent_with_requires(std::string_view input_field,
                                               std::string_view requires_clause) {
    return "module app::main;\n"
           "import std::collections as collections;\n\n"
           "struct Request { " +
           std::string{input_field} +
           " }\n"
           "struct Context { value: Int = 0; }\n"
           "struct Response { ok: Bool; }\n\n"
           "agent A {\n"
           "    input: Request;\n"
           "    context: Context;\n"
           "    output: Response;\n"
           "    states: [Init, Done];\n"
           "    initial: Init;\n"
           "    final: [Done];\n"
           "    capabilities: [];\n"
           "    transition Init -> Done;\n"
           "}\n\n"
           "contract for A {\n"
           "    requires: " +
           std::string{requires_clause} +
           ";\n"
           "    ensures: 1 + 1 == 2;\n"
           "    invariant: always (true);\n"
           "}\n\n"
           "flow for A {\n"
           "    state Init { goto Done; }\n"
           "    state Done { return Response { ok: true, }; }\n"
           "}\n";
}

} // anonymous namespace

// =============================================================================
// Positive: typecheck + lowering
// =============================================================================

TEST_CASE("forall over List binds the element type and lowers to ir::QuantifierExpr") {
    const auto source =
        agent_with_requires("values: collections::List<Int>;", "forall x in input.values: x > 0");
    const auto a = compile_project("forall_list", source);
    REQUIRE_FALSE(a.parse.has_errors());
    REQUIRE_FALSE(a.resolve.has_errors());
    REQUIRE_FALSE(a.tc.has_errors());

    const auto program = ahfl::lower_program_ir(a.parse.graph, a.resolve, a.tc,
                                                /*include_stdlib=*/false);
    CHECK(ir_has_quantifier(program, ahfl::ir::QuantifierExpr::Kind::ForAll));
}

TEST_CASE("exists over Set lowers to an exists ir::QuantifierExpr") {
    const auto source =
        agent_with_requires("tags: collections::Set<Int>;", "exists x in input.tags: x == 7");
    const auto a = compile_project("exists_set", source);
    REQUIRE_FALSE(a.tc.has_errors());

    const auto program = ahfl::lower_program_ir(a.parse.graph, a.resolve, a.tc,
                                                /*include_stdlib=*/false);
    CHECK(ir_has_quantifier(program, ahfl::ir::QuantifierExpr::Kind::Exists));
}

TEST_CASE("forall over Map binds the (key, value) pair") {
    const auto source = agent_with_requires("scores: collections::Map<Int, Int>;",
                                            "forall (k, v) in input.scores: k <= v");
    const auto a = compile_project("forall_map", source);
    CHECK_FALSE(a.tc.has_errors());

    const auto program = ahfl::lower_program_ir(a.parse.graph, a.resolve, a.tc,
                                                /*include_stdlib=*/false);
    CHECK(ir_has_quantifier(program, ahfl::ir::QuantifierExpr::Kind::ForAll));
}

// =============================================================================
// Negative: typecheck diagnostics
// =============================================================================

TEST_CASE("quantifier over a non-collection operand is rejected") {
    const auto source = agent_with_requires("n: Int;", "forall x in input.n: x > 0");
    const auto a = compile_project("forall_non_collection", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.QUANTIFIER_REQUIRES_COLLECTION") == 1);
}

TEST_CASE("quantifier with a non-Bool body is rejected") {
    const auto source = agent_with_requires("values: collections::List<Int>;",
                                            "forall x in input.values: x + 1");
    const auto a = compile_project("forall_non_bool_body", source);
    CHECK(a.tc.has_errors());
    CHECK(diagnostic_count_with_code(a.tc.diagnostics,
                                     "typecheck.QUANTIFIER_BODY_REQUIRES_BOOL") == 1);
}

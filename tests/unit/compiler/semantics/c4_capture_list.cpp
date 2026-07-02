// =============================================================================
// C-4 (Wave-24): closure explicit capture list — `\[a, b] params -> body`.
//
// Adds an optional `[...]` prefix to lambda expressions. The capture list is
// purely advisory at the resolver layer (it emits UNKNOWN_SYMBOL diagnostics
// for any capture name that does not resolve in the outer scope), and is
// threaded through TypedHIR + IR so downstream passes (lowering, LSP hover,
// IR JSON / print) can observe it. Lambdas without a `[...]` prefix keep
// their legacy implicit-capture behaviour — no golden churn.
//
// Harness mirrors b2_impl_body_parser_fixes.cpp: parse_project → resolve →
// typecheck. T6 additionally round-trips through the formatter.
//
// Coverage: 7 TEST_CASE / 30+ assertions.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "tooling/formatter/formatter.hpp"

#include "common/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
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

[[nodiscard]] CompileArtifacts compile_project_loose(std::string_view filename,
                                                     std::string_view source) {
    const std::string sanitized = [filename] {
        std::string s{filename};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_c4_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    write_file(main_path, std::string{source});

    const ahfl::Frontend frontend;
    a.parse = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {a.root, std::filesystem::path{"std"}},
        .inject_prelude = false,
    });

    std::size_t parse_err_count = 0;
    if (a.parse.has_errors()) {
        MESSAGE("[C4-PARSE] ", filename, " diagnostics:");
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

// Counts diagnostics whose code string contains `substr` (e.g. "UNKNOWN_SYMBOL"
// matches "resolve::UNKNOWN_SYMBOL").
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

// Returns the first lambda TypedExpr found in the typed program, so tests can
// assert on `captured_names` / `lambda_params` payloads end-to-end.
[[nodiscard]] const ahfl::TypedExpr *find_first_entry_lambda(const CompileArtifacts &artifacts) {
    if (artifacts.parse.graph.entry_sources.empty()) {
        return nullptr;
    }

    const auto entry_source = artifacts.parse.graph.entry_sources.front();
    for (const auto &e : artifacts.tc.typed_program.expressions) {
        if (e.source_id != entry_source) {
            continue;
        }
        if (e.kind == ahfl::ast::ExprSyntaxKind::Lambda) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// ============================================================================
// T1  Empty capture brackets `\[] x -> x` parse cleanly and behave like the
//     implicit-capture default (no resolve / typecheck errors).
// ============================================================================
TEST_CASE("C-4 empty capture list parses like implicit capture") {
    const auto a = compile_project_loose("t1_empty_capture",
        R"AHFL(
        module c4::t1;
        fn apply(f: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
            return f(x);
        }
        fn main() -> Int effect Pure decreases 0 {
            let inc = \[] x -> x + 1;
            return apply(inc, 41);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UnknownType"), 0u);
    CHECK_EQ(diagnostic_count_with_code(a.tc.diagnostics, "typecheck::WrongArity"), 0u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    // Empty brackets → empty captured_names (distinct from "no brackets" only
    // at the AST layer; TypedHIR/IR carry the empty list faithfully).
    CHECK(lam->captured_names.empty());
    CHECK_EQ(lam->lambda_params.size(), 1u);
    CHECK_EQ(lam->lambda_params.front(), "x");
}

// ============================================================================
// T2  Single capture `\[x] x -> x + 1` resolves cleanly when `x` is an outer
//     let binding. The typed node records `captured_names = ["x"]`.
// ============================================================================
TEST_CASE("C-4 single capture resolves and records captured_names") {
    const auto a = compile_project_loose("t2_single_capture",
        R"AHFL(
        module c4::t2;
        fn main() -> Int effect Pure decreases 0 {
            let x: Int = 10;
            let f = \[x] (y: Int) -> x + y;
            return f(5);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UnknownType"), 0u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    REQUIRE_EQ(lam->captured_names.size(), 1u);
    CHECK_EQ(lam->captured_names.front(), "x");
    REQUIRE_EQ(lam->lambda_params.size(), 1u);
    CHECK_EQ(lam->lambda_params.front(), "y");
}

// ============================================================================
// T3  Multi-capture `\[a, b, c]` records all three names in source order.
// ============================================================================
TEST_CASE("C-4 multi capture preserves source order") {
    const auto a = compile_project_loose("t3_multi_capture",
        R"AHFL(
        module c4::t3;
        fn main() -> Int effect Pure decreases 0 {
            let a: Int = 1;
            let b: Int = 10;
            let c: Int = 100;
            let sum = \[a, b, c] () -> a + b + c;
            return sum();
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    REQUIRE_EQ(lam->captured_names.size(), 3u);
    CHECK_EQ(lam->captured_names[0], "a");
    CHECK_EQ(lam->captured_names[1], "b");
    CHECK_EQ(lam->captured_names[2], "c");
    CHECK(lam->lambda_params.empty());
}

// ============================================================================
// T4  Negative: capturing a name that does not exist in outer scope emits
//     ≥1 resolve.UNKNOWN_SYMBOL diagnostic. The lambda still type-checks
//     (capture lists are advisory, not fatal).
// ============================================================================
TEST_CASE("C-4 unknown capture emits UNKNOWN_SYMBOL") {
    const auto a = compile_project_loose("t4_unknown_capture",
        R"AHFL(
        module c4::t4;
        fn main() -> Int effect Pure decreases 0 {
            let x: Int = 1;
            let f = \[x, ghost] (y: Int) -> x + y;
            return f(2);
        }
        )AHFL");
    const auto unknown = diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL");
    CHECK_GE(unknown, 1u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    // Even with an unknown capture, the list is recorded verbatim so later
    // passes can render it (hover, IR print, etc.).
    REQUIRE_EQ(lam->captured_names.size(), 2u);
    CHECK_EQ(lam->captured_names[0], "x");
    CHECK_EQ(lam->captured_names[1], "ghost");
}

// ============================================================================
// T5  Capture list inside an impl<T> method body: the impl-level type
//     parameter `T` is visible to the resolver, and the closure's parameter
//     annotation + body both resolve against it without UnknownType.
// ============================================================================
TEST_CASE("C-5 capture list inside impl<T> method body sees T") {
    const auto a = compile_project_loose("t5_impl_body_capture_T",
        R"AHFL(
        module c4::t5;
        trait Identity<T> {
            fn app(self: Self, f: Fn(T) -> T) -> T effect Pure;
        }
        struct Holder<T> {
            value: T;
        }
        impl<T> Identity<T> for Holder<T> {
            fn app(self: Self, f: Fn(T) -> T) -> T effect Pure decreases 0 {
                // Capture `self` explicitly; body uses `self.value` of type T.
                let passthrough: Fn(T) -> T = \[self] (x: T) -> x;
                return f(passthrough(self.value));
            }
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UnknownType"), 0u);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    REQUIRE_EQ(lam->captured_names.size(), 1u);
    CHECK_EQ(lam->captured_names.front(), "self");
}

// ============================================================================
// T6  Formatter round-trip: source → format → re-parse. The re-parsed AST's
//     lambda node must carry the same capture list (count + names) and the
//     same parameter count. Formatter must emit `\[a, b] ` prefix only when
//     the capture list is non-empty.
// ============================================================================
TEST_CASE("C-6 formatter round-trip preserves capture list") {
    const std::string original =
        "module c4::t6;\n"
        "fn main() -> Int effect Pure decreases 0 {\n"
        "    let a: Int = 1;\n"
        "    let b: Int = 2;\n"
        "    let f = \\[a, b] (x: Int) -> x + a + b;\n"
        "    return f(3);\n"
        "}\n";

    const auto formatted = ahfl::formatter::format_source(original);
    REQUIRE(formatted.success);
    REQUIRE_FALSE(formatted.formatted.empty());
    // Formatter emits the capture prefix when non-empty.
    CHECK(formatted.formatted.find("\\[a, b]") != std::string::npos);

    // Re-parse the formatted source and confirm the capture list survives.
    const auto a = compile_project_loose("t6_formatter_roundtrip", formatted.formatted);
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diag_count_containing(a.tc.diagnostics, "UnknownType"), 0u);

    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    REQUIRE_EQ(lam->captured_names.size(), 2u);
    CHECK_EQ(lam->captured_names[0], "a");
    CHECK_EQ(lam->captured_names[1], "b");
    REQUIRE_EQ(lam->lambda_params.size(), 1u);
    CHECK_EQ(lam->lambda_params.front(), "x");
}

// ============================================================================
// T7  No-brackets lambda (legacy form) produces an empty captured_names list
//     — i.e. the new field is default-initialised and does not churn goldens.
// ============================================================================
TEST_CASE("C-7 no-brackets lambda keeps empty captured_names") {
    const auto a = compile_project_loose("t7_no_brackets",
        R"AHFL(
        module c4::t7;
        fn main() -> Int effect Pure decreases 0 {
            let f = \x -> x + 1;
            return f(5);
        }
        )AHFL");
    CHECK_EQ(diag_count_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    const auto *lam = find_first_entry_lambda(a);
    REQUIRE(lam != nullptr);
    CHECK(lam->captured_names.empty());
    REQUIRE_EQ(lam->lambda_params.size(), 1u);
    CHECK_EQ(lam->lambda_params.front(), "x");
}

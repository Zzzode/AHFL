// =============================================================================
// D-3 (Wave-24): effect-clause `decreases X` — from literal-only to any
// Int-typed expression.
//
// Before D-3 the signature-level effect clause only accepted `decreases 0`
// (a literal integer).  This change generalises the measure to any
// well-typed Pure Int expression, enabling patterns like:
//
//     fn length<T>(xs: List<T>) -> Int effect Pure decreases xs.count { ... }
//     impl<T> List<T> { fn length(...) effect Pure decreases self.count { ... } }
//
// Harness mirrors b2_impl_body_parser_fixes.cpp: parse_project → resolve →
// typecheck.  The typecheck pass (FnSema / ImplSema) validates the decreases
// expression against the body's ValueContext (which carries parameter
// bindings + Self/self for impl methods).
//
// Coverage: 6 TEST_CASE / ≥30 assertions.
// =============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

// Build a human-readable one-line summary of a diagnostic.
[[nodiscard]] std::string diag_one_liner(const ahfl::Diagnostic &d) {
    std::ostringstream oss;
    oss << "[" << to_string(d.severity) << "]";
    if (d.code) {
        oss << " code=" << d.code.value();
    }
    oss << " " << d.message;
    return oss.str();
}

// Dump all diagnostics of a bag to doctest MESSAGE lines (prefixed with tag).
void dump_diags(const char *tag, const ahfl::DiagnosticBag &bag) {
    for (const auto &d : bag.entries()) {
        MESSAGE(tag, diag_one_liner(d));
    }
}

[[nodiscard]] CompileArtifacts compile_project_loose(std::string_view filename,
                                                     std::string_view source) {
    const std::string sanitized = [filename] {
        std::string s{filename};
        std::replace(s.begin(), s.end(), '/', '_');
        std::replace(s.begin(), s.end(), '.', '_');
        return s;
    }();
    CompileArtifacts a;
    a.root = std::filesystem::temp_directory_path() / ("ahfl_d3_" + sanitized);
    std::filesystem::remove_all(a.root);
    const auto main_path = a.root / "app" / "main.ahfl";
    write_file(main_path, std::string{source});

    const ahfl::Frontend frontend;
    a.parse = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {a.root, std::filesystem::path{"std"}},
        .inject_prelude = true,
    });

    std::size_t parse_err_count = 0;
    for (const auto &d : a.parse.diagnostics.entries()) {
        if (d.severity == ahfl::DiagnosticSeverity::Error) {
            ++parse_err_count;
        }
    }
    if (parse_err_count > 0) {
        dump_diags("[D3-PARSE] ", a.parse.diagnostics);
    }
    REQUIRE_EQ(parse_err_count, 0u);
    REQUIRE_FALSE(a.parse.graph.entry_sources.empty());

    const ahfl::Resolver resolver;
    a.resolve = resolver.resolve(a.parse.graph);

    const ahfl::TypeChecker type_checker;
    a.tc = type_checker.check(a.parse.graph, a.resolve, {});

    // Keep this helper focused on D-3 diagnostics; assertions below filter by
    // stable diagnostic code/message substrings relevant to the feature.
    return a;
}

// Count diagnostics whose code string contains `substr`.
[[nodiscard]] std::size_t diag_code_containing(const ahfl::DiagnosticBag &bag,
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

// Count diagnostics whose message contains `substr` (useful for
// "decreases clause" origin-tagged TypeMismatch messages).
[[nodiscard]] std::size_t diag_message_containing(const ahfl::DiagnosticBag &bag,
                                                  std::string_view substr) {
    std::size_t count = 0;
    for (const auto &d : bag.entries()) {
        if (d.severity != ahfl::DiagnosticSeverity::Error) continue;
        const std::string msg = d.message;
        if (msg.find(substr) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

} // namespace

// ============================================================================
// T1  Literal integers 0 / 1 / 100 remain valid (baseline regression —
//     "decreases 0" was the only accepted form before D-3).
// ============================================================================
TEST_CASE("D-3 literal decreases measures remain valid") {
    SUBCASE("decreases 0 — baseline") {
        const auto a = compile_project_loose("t1_zero",
            R"AHFL(
            module d3::t1_zero;
            fn countdown(n: Int) -> Int effect Pure decreases 0 {
                return n;
            }
            fn main() -> Int effect Pure decreases 0 {
                return countdown(5);
            }
            )AHFL");
        // Verify there are no decreases errors and all symbols resolve.
        CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
        CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    }

    SUBCASE("decreases 1 — positive literal") {
        const auto a = compile_project_loose("t1_one",
            R"AHFL(
            module d3::t1_one;
            fn f() -> Int effect Pure decreases 1 {
                return 42;
            }
            )AHFL");
        CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
        CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    }

    SUBCASE("decreases 100 — larger literal") {
        const auto a = compile_project_loose("t1_hundred",
            R"AHFL(
            module d3::t1_hundred;
            fn f() -> Int effect Pure decreases 100 {
                return 0;
            }
            )AHFL");
        CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
        CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    }
}

// ============================================================================
// T2  Variable `decreases n` refers to an in-scope Int parameter.
//     This is the primary D-3 use case: parameter-based termination measure.
// ============================================================================
TEST_CASE("D-3 decreases with Int parameter variable") {
    const auto a = compile_project_loose("t2_param_var",
        R"AHFL(
        module d3::t2;
        fn countdown(n: Int) -> Int effect Pure decreases n {
            if (n <= 0) {
                return 0;
            }
            return countdown(n - 1);
        }
        fn main() -> Int effect Pure decreases 0 {
            return countdown(10);
        }
        )AHFL");
    // No decreases errors (n is Int and resolves).
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
    // The parameter `n` resolves correctly (no unknown symbol).
    CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    // No UNKNOWN_TYPE either.
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
}

// ============================================================================
// T3  Binary expression measure:
//       `decreases n + 1`        — binary op on Int param
// ============================================================================
TEST_CASE("D-3 decreases with binary expression measure") {
    const auto a = compile_project_loose("t3_binary",
        R"AHFL(
        module d3::t3;
        fn bounded_count(n: Int) -> Int effect Pure decreases n + 1 {
            if (n <= 0) {
                return 0;
            }
            return 1 + bounded_count(n - 1);
        }
        fn main() -> Int effect Pure decreases 0 {
            return bounded_count(3);
        }
        )AHFL");
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
    CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
}

// ============================================================================
// T4  Negative: decreases "abc" — string literal must produce TypeMismatch
//     with origin tagged "decreases clause".
// ============================================================================
TEST_CASE("D-3 decreases with string literal emits TypeMismatch") {
    const auto a = compile_project_loose("t4_string_neg",
        R"AHFL(
        module d3::t4;
        fn bad() -> Int effect Pure decreases "abc" {
            return 0;
        }
        )AHFL");
    // Must emit at least one TypeMismatch error.
    CHECK_GT(diag_code_containing(a.tc.diagnostics, "TYPE_MISMATCH"), 0u);
    // The error message should reference "decreases clause" (D-3 origin tag).
    CHECK_GT(diag_message_containing(a.tc.diagnostics, "decreases clause"), 0u);
}

// ============================================================================
// T5  Negative: decreases x where x is Bool (not Int) — must produce
//     TypeMismatch with origin tagged "decreases clause".
// ============================================================================
TEST_CASE("D-3 decreases with Bool variable emits TypeMismatch") {
    const auto a = compile_project_loose("t5_bool_neg",
        R"AHFL(
        module d3::t5;
        fn bad(flag: Bool) -> Int effect Pure decreases flag {
            return 0;
        }
        )AHFL");
    CHECK_GT(diag_code_containing(a.tc.diagnostics, "TYPE_MISMATCH"), 0u);
    CHECK_GT(diag_message_containing(a.tc.diagnostics, "decreases clause"), 0u);
    // `flag` itself resolves fine (no UNKNOWN_SYMBOL).
    CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
}

// ============================================================================
// T6  Impl-method decreases: `impl Counter` method uses self in the
//     decreases measure.  Mirrors trait_runtime_smoke integration pattern.
//     NOTE: "next" is a reserved keyword (temporal expression), so we use
//     "increment" as the method name.
// ============================================================================
TEST_CASE("D-3 impl method decreases with self field access") {
    const auto a = compile_project_loose("t6_impl_self",
        R"AHFL(
        module d3::t6;
        struct Counter {
            value: Int;
        }
        impl Counter {
            fn increment(self: Counter) -> Int effect Pure decreases self.value {
                return self.value;
            }
        }
        fn main() -> Int effect Pure decreases 0 {
            let c = Counter { value: 7 };
            return c.increment();
        }
        )AHFL");
    // self.value is Int → decreases accepted.
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "NO_DECREASES"), 0u);
    // Self/self resolves (no unknown type / symbol errors from the impl body).
    CHECK_EQ(diag_code_containing(a.tc.diagnostics, "UNKNOWN_TYPE"), 0u);
    CHECK_EQ(diag_code_containing(a.resolve.diagnostics, "UNKNOWN_SYMBOL"), 0u);
}

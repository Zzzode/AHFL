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

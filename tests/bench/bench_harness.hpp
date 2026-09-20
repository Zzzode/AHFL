#pragma once

// KR7.3: shared harness for the compile-time and memory-proxy quality benches.
//
// Both benches compile the same fixture ladder and differ only in which budgets
// / ceilings they close over, so the measurement *row* and the opt-in
// machine-readable report live here once instead of being duplicated (and
// silently allowed to drift) per binary. This is also the single definition of
// the `ahfl.bench-report.v1` schema that
// `tests/scripts/benchmark_trend_gate.py` consumes.
//
// Report emission is opt-in and side-effect-free when not requested: either
// `--report <path>` / `--report=<path>` on the command line, or the
// `AHFL_BENCH_REPORT` environment variable. The default stdout gate is
// unaffected. The trend gate drives both binaries through this channel with a
// scratch path and diffs the rows against `config/benchmark-baseline.json`.
//
// Lexical rules follow the repository's existing `PrettyJsonWriter` (2-space
// indent) — the same writer the IR / assurance / counterexample JSON artifacts
// use — so one JSON spelling exists across all committed artifacts.
//
// This header deliberately stays dependency-free: it includes only the standard
// library and the public fixture header, so including it never requires the
// src-private include root (`${PROJECT_SOURCE_DIR}/src`) that
// `base/support/json.hpp` needs. That private dependency lives in exactly one
// translation unit, `bench_harness.cpp`, and is linked in through the
// `ahfl_bench_harness` target. Keeping it in a .cpp mirrors how every other
// src-internal consumer takes the private header directly and means a new bench
// that includes this header cannot fail to compile for an undeclared include
// root.

#include "bench_compiler_fixture.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::bench {

inline constexpr std::string_view kBenchReportSchema = "ahfl.bench-report.v1";

// One measured module. Both benches fill every field, so the two reports are
// directly comparable and the trend gate guards any budget without a second row
// type. `proxy_bytes` is `estimate_proxy_bytes` (bench_compiler_fixture.hpp),
// i.e. one proxy definition across both binaries.
//
// `duration` stays a `std::chrono::microseconds`, not a bare integer, so the
// budget checks compare it against a `std::chrono::milliseconds` ceiling with
// chrono's unit handling (the unit is the type, not a printf convention); it is
// serialized as `duration_us` via `.count()`.
struct BenchMetric {
    std::string name;
    std::size_t source_bytes{};
    std::size_t typed_exprs{};
    std::size_t typed_statements{};
    std::size_t ir_decls{};
    std::size_t ir_exprs{};
    std::size_t proxy_bytes{};
    std::chrono::microseconds duration{};
};

[[nodiscard]] inline BenchMetric make_bench_metric(const std::string &name,
                                                   const CompileArtifacts &artifacts,
                                                   std::chrono::microseconds duration) {
    const auto &typed = artifacts.type_result.typed_program;
    return BenchMetric{
        .name = name,
        .source_bytes = artifacts.source.size(),
        .typed_exprs = typed.expressions.size(),
        .typed_statements = typed.statements.size(),
        .ir_decls = artifacts.ir_program.declarations.size(),
        .ir_exprs = artifacts.ir_program.expr_arena.size(),
        .proxy_bytes = estimate_proxy_bytes(artifacts),
        .duration = duration,
    };
}

// Parse the report request from the command line, falling back to the
// environment. Returns nullopt when no report was requested (the default).
[[nodiscard]] inline std::optional<std::string> bench_report_path(int argc, char **argv) {
    constexpr std::string_view flag = "--report";

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == flag) {
            return (index + 1 < argc) ? std::optional<std::string>{argv[index + 1]}
                                      : std::optional<std::string>{std::string{}};
        }
        if (argument.starts_with("--report=")) {
            return std::string(argument.substr(flag.size() + 1));
        }
    }

    if (const char *from_env = std::getenv("AHFL_BENCH_REPORT");
        from_env != nullptr && *from_env != '\0') {
        return std::string{from_env};
    }
    return std::nullopt;
}

// Serialize `metrics` to `path` as the `ahfl.bench-report.v1` document. Defined
// in bench_harness.cpp, the one TU that owns the private JSON-writer dependency
// (`base/support/json.hpp`). Returns false when the file cannot be created or
// written, so a caller can turn a bad report path into a hard failure rather
// than silently losing the artifact the trend gate depends on.
[[nodiscard]] bool write_bench_report(const std::string &path, std::string_view kind,
                                      const std::vector<BenchMetric> &metrics);

// The PASS/FAIL tally both benches print. Kept here so the two binaries share
// one definition of "a bench check" and one exit-code policy.
class BenchCheck {
  public:
    void operator()(bool condition, std::string_view name) {
        ++total_;
        const char *marker = "FAIL";
        if (condition) {
            ++passed_;
            marker = "PASS";
        }
        std::printf("  %s: %.*s\n", marker, static_cast<int>(name.size()), name.data());
    }

    void summarize() const { std::printf("\n%zu/%zu tests passed\n", passed_, total_); }

    [[nodiscard]] bool all_passed() const { return passed_ == total_; }

  private:
    std::size_t total_{0};
    std::size_t passed_{0};
};

} // namespace ahfl::bench

#include "bench_compiler_fixture.hpp"

#include "ahfl/compiler/ir/verify.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name);
    } else {
        std::printf("  FAIL: %s\n", name);
    }
}

namespace {

using Clock = std::chrono::steady_clock;

struct CompileBudget {
    std::string name;
    std::size_t let_count;
    std::size_t branch_count;
    std::chrono::milliseconds max_duration;
};

struct CompileMeasurement {
    std::string name;
    std::size_t source_bytes{};
    std::size_t typed_exprs{};
    std::size_t ir_exprs{};
    std::chrono::microseconds duration{};
    std::chrono::milliseconds max_duration{};
};

CompileMeasurement measure_compile_budget(const CompileBudget &budget) {
    const auto start = Clock::now();
    auto artifacts = ahfl::bench::compile_benchmark_source(budget.let_count, budget.branch_count);
    const auto end = Clock::now();

    check(!artifacts.resolve_result.has_errors(), (budget.name + " resolve succeeds").c_str());
    check(!artifacts.type_result.has_errors(), (budget.name + " typecheck succeeds").c_str());
    check(!artifacts.validation_result.has_errors(), (budget.name + " validate succeeds").c_str());
    check(!ahfl::ir::verify_ir_program(artifacts.ir_program).has_errors(),
          (budget.name + " IR verifies").c_str());

    return CompileMeasurement{
        .name = budget.name,
        .source_bytes = artifacts.source.size(),
        .typed_exprs = artifacts.type_result.typed_program.expressions.size(),
        .ir_exprs = artifacts.ir_program.expr_arena.size(),
        .duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start),
        .max_duration = budget.max_duration,
    };
}

} // namespace

int main() {
    std::printf("=== Compile Time Budget Gate ===\n");

    const std::vector<CompileBudget> budgets{
        {.name = "small_module",
         .let_count = 32,
         .branch_count = 8,
         .max_duration = std::chrono::milliseconds{750}},
        {.name = "medium_module",
         .let_count = 160,
         .branch_count = 48,
         .max_duration = std::chrono::milliseconds{2500}},
        {.name = "large_module",
         .let_count = 320,
         .branch_count = 96,
         .max_duration = std::chrono::milliseconds{6000}},
    };

    std::vector<CompileMeasurement> measurements;
    measurements.reserve(budgets.size());
    for (const auto &budget : budgets) {
        measurements.push_back(measure_compile_budget(budget));
    }

    check(measurements[0].duration <= measurements[0].max_duration,
          "small module stays within compile budget");
    check(measurements[1].duration <= measurements[1].max_duration,
          "medium module stays within compile budget");
    check(measurements[2].duration <= measurements[2].max_duration,
          "large module stays within compile budget");
    check(measurements[1].typed_exprs > measurements[0].typed_exprs,
          "medium module has more typed expressions than small");
    check(measurements[2].typed_exprs > measurements[1].typed_exprs,
          "large module has more typed expressions than medium");

    std::printf("\nResults:\n");
    for (const auto &measurement : measurements) {
        std::printf("  %-14s %zu bytes, %zu typed exprs, %zu IR exprs, %lld us (budget %lld ms)\n",
                    measurement.name.c_str(),
                    measurement.source_bytes,
                    measurement.typed_exprs,
                    measurement.ir_exprs,
                    static_cast<long long>(measurement.duration.count()),
                    static_cast<long long>(measurement.max_duration.count()));
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

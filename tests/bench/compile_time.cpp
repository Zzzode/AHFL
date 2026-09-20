#include "bench_harness.hpp"

#include "ahfl/compiler/ir/verify.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct CompileBudget {
    std::string name;
    std::size_t let_count;
    std::size_t branch_count;
    std::chrono::milliseconds max_duration;
};

struct CompileMeasurement {
    ahfl::bench::BenchMetric metric;
    std::chrono::milliseconds max_duration{};
};

CompileMeasurement measure_compile_budget(ahfl::bench::BenchCheck &check,
                                          const CompileBudget &budget) {
    const auto start = Clock::now();
    auto artifacts = ahfl::bench::compile_benchmark_source(budget.let_count, budget.branch_count);
    const auto end = Clock::now();

    check(!artifacts.resolve_result.has_errors(), budget.name + " resolve succeeds");
    check(!artifacts.type_result.has_errors(), budget.name + " typecheck succeeds");
    check(!artifacts.validation_result.has_errors(), budget.name + " validate succeeds");
    check(!ahfl::ir::verify_ir_program(artifacts.ir_program).has_errors(),
          budget.name + " IR verifies");

    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return CompileMeasurement{
        .metric = ahfl::bench::make_bench_metric(budget.name, artifacts, duration),
        .max_duration = budget.max_duration,
    };
}

} // namespace

int main(int argc, char **argv) {
    std::printf("=== Compile Time Budget Gate ===\n");
    ahfl::bench::BenchCheck check;

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
        measurements.push_back(measure_compile_budget(check, budget));
    }

    check(measurements[0].metric.duration <= measurements[0].max_duration,
          "small module stays within compile budget");
    check(measurements[1].metric.duration <= measurements[1].max_duration,
          "medium module stays within compile budget");
    check(measurements[2].metric.duration <= measurements[2].max_duration,
          "large module stays within compile budget");
    check(measurements[1].metric.typed_exprs > measurements[0].metric.typed_exprs,
          "medium module has more typed expressions than small");
    check(measurements[2].metric.typed_exprs > measurements[1].metric.typed_exprs,
          "large module has more typed expressions than medium");

    std::printf("\nResults:\n");
    for (const auto &measurement : measurements) {
        const auto &metric = measurement.metric;
        std::printf("  %-14s %zu bytes, %zu typed exprs, %zu IR exprs, %lld us (budget %lld ms)\n",
                    metric.name.c_str(),
                    metric.source_bytes,
                    metric.typed_exprs,
                    metric.ir_exprs,
                    static_cast<long long>(metric.duration.count()),
                    static_cast<long long>(measurement.max_duration.count()));
    }

    if (const auto report_path = ahfl::bench::bench_report_path(argc, argv);
        report_path.has_value()) {
        std::vector<ahfl::bench::BenchMetric> metrics;
        metrics.reserve(measurements.size());
        for (const auto &measurement : measurements) {
            metrics.push_back(measurement.metric);
        }
        if (!ahfl::bench::write_bench_report(*report_path, "compile_time", metrics)) {
            std::printf("  FAIL: could not write bench report to %s\n", report_path->c_str());
            return 1;
        }
        std::printf("  report: %s\n", report_path->c_str());
    }

    check.summarize();
    return check.all_passed() ? 0 : 1;
}

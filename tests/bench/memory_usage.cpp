#include "bench_harness.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct MemoryBudget {
    std::string name;
    std::size_t let_count;
    std::size_t branch_count;
    std::size_t max_proxy_bytes;
};

struct MemoryMeasurement {
    ahfl::bench::BenchMetric metric;
    std::size_t max_proxy_bytes{};
};

MemoryMeasurement measure_memory_budget(ahfl::bench::BenchCheck &check,
                                        const MemoryBudget &budget) {
    const auto start = std::chrono::steady_clock::now();
    auto artifacts = ahfl::bench::compile_benchmark_source(budget.let_count, budget.branch_count);
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    check(!artifacts.resolve_result.has_errors(), budget.name + " resolve succeeds");
    check(!artifacts.type_result.has_errors(), budget.name + " typecheck succeeds");
    check(!artifacts.validation_result.has_errors(), budget.name + " validate succeeds");

    return MemoryMeasurement{
        .metric = ahfl::bench::make_bench_metric(budget.name, artifacts, duration),
        .max_proxy_bytes = budget.max_proxy_bytes,
    };
}

} // namespace

int main(int argc, char **argv) {
    std::printf("=== Memory Proxy Budget Gate ===\n");
    ahfl::bench::BenchCheck check;

    const std::vector<MemoryBudget> budgets{
        {.name = "small_module", .let_count = 32, .branch_count = 8, .max_proxy_bytes = 128 * 1024},
        {.name = "medium_module",
         .let_count = 160,
         .branch_count = 48,
         .max_proxy_bytes = 512 * 1024},
        {.name = "large_module",
         .let_count = 320,
         .branch_count = 96,
         .max_proxy_bytes = 1024 * 1024},
    };

    std::vector<MemoryMeasurement> measurements;
    measurements.reserve(budgets.size());
    for (const auto &budget : budgets) {
        measurements.push_back(measure_memory_budget(check, budget));
    }

    check(measurements[0].metric.proxy_bytes <= measurements[0].max_proxy_bytes,
          "small module stays within proxy budget");
    check(measurements[1].metric.proxy_bytes <= measurements[1].max_proxy_bytes,
          "medium module stays within proxy budget");
    check(measurements[2].metric.proxy_bytes <= measurements[2].max_proxy_bytes,
          "large module stays within proxy budget");
    check(measurements[1].metric.proxy_bytes > measurements[0].metric.proxy_bytes,
          "medium module proxy is larger than small");
    check(measurements[2].metric.proxy_bytes > measurements[1].metric.proxy_bytes,
          "large module proxy is larger than medium");

    std::printf("\nResults:\n");
    for (const auto &measurement : measurements) {
        const auto &metric = measurement.metric;
        std::printf("  %-14s proxy=%zu/%zu bytes, source=%zu, typed_exprs=%zu, "
                    "typed_statements=%zu, ir_decls=%zu, ir_exprs=%zu\n",
                    metric.name.c_str(),
                    metric.proxy_bytes,
                    measurement.max_proxy_bytes,
                    metric.source_bytes,
                    metric.typed_exprs,
                    metric.typed_statements,
                    metric.ir_decls,
                    metric.ir_exprs);
    }

    if (const auto report_path = ahfl::bench::bench_report_path(argc, argv);
        report_path.has_value()) {
        std::vector<ahfl::bench::BenchMetric> metrics;
        metrics.reserve(measurements.size());
        for (const auto &measurement : measurements) {
            metrics.push_back(measurement.metric);
        }
        if (!ahfl::bench::write_bench_report(*report_path, "memory_usage", metrics)) {
            std::printf("  FAIL: could not write bench report to %s\n", report_path->c_str());
            return 1;
        }
        std::printf("  report: %s\n", report_path->c_str());
    }

    check.summarize();
    return check.all_passed() ? 0 : 1;
}

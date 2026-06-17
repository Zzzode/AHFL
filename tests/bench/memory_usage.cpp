#include "bench_compiler_fixture.hpp"

#include <cstdio>
#include <string>
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

struct MemoryBudget {
    std::string name;
    std::size_t let_count;
    std::size_t branch_count;
    std::size_t max_proxy_bytes;
};

struct MemoryMeasurement {
    std::string name;
    std::size_t source_bytes{};
    std::size_t typed_exprs{};
    std::size_t typed_statements{};
    std::size_t ir_decls{};
    std::size_t ir_exprs{};
    std::size_t proxy_bytes{};
    std::size_t max_proxy_bytes{};
};

std::size_t estimate_proxy_bytes(const ahfl::bench::CompileArtifacts &artifacts) {
    const auto &typed = artifacts.type_result.typed_program;
    const auto &ir_program = artifacts.ir_program;
    return artifacts.source.size() + typed.declarations.size() * 256 +
           typed.expressions.size() * 256 + typed.blocks.size() * 192 +
           typed.statements.size() * 192 + typed.temporal_exprs.size() * 192 +
           typed.symbols.size() * 160 + typed.references.size() * 160 +
           ir_program.declarations.size() * 512 + ir_program.expr_arena.size() * 256;
}

MemoryMeasurement measure_memory_budget(const MemoryBudget &budget) {
    auto artifacts = ahfl::bench::compile_benchmark_source(budget.let_count, budget.branch_count);
    check(!artifacts.resolve_result.has_errors(), (budget.name + " resolve succeeds").c_str());
    check(!artifacts.type_result.has_errors(), (budget.name + " typecheck succeeds").c_str());
    check(!artifacts.validation_result.has_errors(), (budget.name + " validate succeeds").c_str());

    const auto &typed = artifacts.type_result.typed_program;
    return MemoryMeasurement{
        .name = budget.name,
        .source_bytes = artifacts.source.size(),
        .typed_exprs = typed.expressions.size(),
        .typed_statements = typed.statements.size(),
        .ir_decls = artifacts.ir_program.declarations.size(),
        .ir_exprs = artifacts.ir_program.expr_arena.size(),
        .proxy_bytes = estimate_proxy_bytes(artifacts),
        .max_proxy_bytes = budget.max_proxy_bytes,
    };
}

} // namespace

int main() {
    std::printf("=== Memory Proxy Budget Gate ===\n");

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
        measurements.push_back(measure_memory_budget(budget));
    }

    check(measurements[0].proxy_bytes <= measurements[0].max_proxy_bytes,
          "small module stays within proxy budget");
    check(measurements[1].proxy_bytes <= measurements[1].max_proxy_bytes,
          "medium module stays within proxy budget");
    check(measurements[2].proxy_bytes <= measurements[2].max_proxy_bytes,
          "large module stays within proxy budget");
    check(measurements[1].proxy_bytes > measurements[0].proxy_bytes,
          "medium module proxy is larger than small");
    check(measurements[2].proxy_bytes > measurements[1].proxy_bytes,
          "large module proxy is larger than medium");

    std::printf("\nResults:\n");
    for (const auto &measurement : measurements) {
        std::printf("  %-14s proxy=%zu/%zu bytes, source=%zu, typed_exprs=%zu, "
                    "typed_statements=%zu, ir_decls=%zu, ir_exprs=%zu\n",
                    measurement.name.c_str(),
                    measurement.proxy_bytes,
                    measurement.max_proxy_bytes,
                    measurement.source_bytes,
                    measurement.typed_exprs,
                    measurement.typed_statements,
                    measurement.ir_decls,
                    measurement.ir_exprs);
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

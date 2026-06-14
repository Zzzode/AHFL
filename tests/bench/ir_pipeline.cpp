#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/ir/visitor.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "compiler/ir/opt/opt_passes.hpp"
#include "compiler/ir/opt/opt_verify.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
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

struct Timing {
    std::string name;
    std::chrono::microseconds duration{};
};

template <typename Fn> auto measure(std::string name, Fn &&fn) {
    const auto start = Clock::now();
    auto value = fn();
    const auto end = Clock::now();
    return std::pair{std::move(value),
                     Timing{std::move(name),
                            std::chrono::duration_cast<std::chrono::microseconds>(end - start)}};
}

std::string make_large_source(std::size_t let_count, std::size_t branch_count) {
    std::ostringstream out;
    out << "module bench::ir_pipeline;\n\n";
    out << "struct Request {\n  value: String;\n}\n\n";
    out << "struct Context {\n  value: String = \"\";\n}\n\n";
    out << "struct Response {\n  value: String;\n}\n\n";
    out << "agent BenchAgent {\n";
    out << "  input: Request;\n";
    out << "  context: Context;\n";
    out << "  output: Response;\n";
    out << "  states: [Done];\n";
    out << "  initial: Done;\n";
    out << "  final: [Done];\n";
    out << "  capabilities: [];\n";
    out << "}\n\n";
    out << "flow for BenchAgent {\n";
    out << "  state Done {\n";
    for (std::size_t index = 0; index < let_count; ++index) {
        out << "    let flag" << index << ": Bool = true;\n";
    }
    for (std::size_t index = 0; index < branch_count; ++index) {
        out << "    if true {\n";
        out << "      let then_flag" << index << ": Bool = true;\n";
        out << "    } else {\n";
        out << "      let else_flag" << index << ": Bool = false;\n";
        out << "    }\n";
    }
    out << "    return Response { value: input.value };\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

ahfl::SourceGraph make_source_graph(ahfl::ParseResult parse_result) {
    ahfl::SourceId source_id{0};
    ahfl::SourceGraph graph;
    graph.entry_sources.push_back(source_id);
    graph.module_to_source.emplace("bench::ir_pipeline", source_id);
    graph.sources.push_back(ahfl::SourceUnit{
        .id = source_id,
        .path = std::filesystem::path{"bench/ir_pipeline.ahfl"},
        .module_name = "bench::ir_pipeline",
        .module_range = {},
        .source = std::move(parse_result.source),
        .program = std::move(parse_result.program),
        .imports = {},
    });
    return graph;
}

class CountingVisitor final : public ahfl::ir::ProgramVisitor {
  public:
    std::size_t expr_count{0};
    std::size_t statement_count{0};

  private:
    void on_expr(const ahfl::ir::Expr & /*expr*/) override {
        ++expr_count;
    }
    void on_statement(const ahfl::ir::Statement & /*statement*/) override {
        ++statement_count;
    }
};

std::size_t run_source_id_lookup_smoke(const ahfl::TypedProgram &program) {
    std::size_t hits = 0;
    for (const auto &expr : program.expressions) {
        if (!expr.source_id.has_value()) {
            continue;
        }
        const auto *found = program.find_expr_by_range(expr.range, expr.source_id);
        if (found == &expr) {
            ++hits;
        }
    }
    return hits;
}

} // namespace

int main() {
    std::printf("=== IR Pipeline Smoke Benchmark ===\n");

    const auto source = make_large_source(160, 48);
    check(source.size() > 0, "benchmark source generated");

    ahfl::Frontend frontend;
    auto [parse_result, parse_timing] =
        measure("parse", [&]() { return frontend.parse_text("bench/ir_pipeline.ahfl", source); });
    check(!parse_result.has_errors(), "parse succeeds");
    check(parse_result.program != nullptr, "parse returns AST");
    if (parse_result.has_errors() || parse_result.program == nullptr) {
        return 1;
    }

    auto graph = make_source_graph(std::move(parse_result));

    ahfl::Resolver resolver;
    auto [resolve_result, resolve_timing] =
        measure("resolve", [&]() { return resolver.resolve(graph); });
    check(!resolve_result.has_errors(), "resolve succeeds");

    ahfl::TypeChecker checker;
    auto [type_result, typecheck_timing] =
        measure("typecheck", [&]() { return checker.check(graph, resolve_result); });
    check(!type_result.has_errors(), "typecheck succeeds");
    check(type_result.typed_program.expressions.size() > 200,
          "typecheck records large expression store");

    ahfl::Validator validator;
    auto [validation_result, validate_timing] = measure(
        "validate", [&]() { return validator.validate(graph, resolve_result, type_result); });
    check(!validation_result.has_errors(), "validate succeeds");

    auto [lookup_hits, lookup_timing] = measure("source-id lookup", [&]() {
        return run_source_id_lookup_smoke(type_result.typed_program);
    });
    check(lookup_hits > 0, "source-id expression lookup has hits");

    auto [ir_program, lowering_timing] = measure("semantic ir lowering", [&]() {
        return ahfl::lower_program_ir(graph, resolve_result, type_result);
    });
    check(!ahfl::ir::verify_ir_program(ir_program).has_errors(), "semantic IR verifies");
    check(ir_program.expr_arena.size() >= type_result.typed_program.expressions.size() / 2,
          "ExprArena receives lowered expressions");

    auto [visitor_counts, visitor_timing] = measure("visitor traversal", [&]() {
        CountingVisitor visitor;
        visitor.visit(ir_program);
        return std::pair{visitor.expr_count, visitor.statement_count};
    });
    check(visitor_counts.first == ir_program.expr_arena.size(), "visitor sees every arena expr");
    check(visitor_counts.second > 0, "visitor sees statements");

    auto [opt_program, opt_lower_timing] =
        measure("opt lowering", [&]() { return ahfl::ir::opt::lower_to_opt(ir_program); });
    check(!ahfl::ir::opt::verify_opt_program(opt_program).has_errors(), "Opt IR verifies");
    check(!opt_program.functions.empty(), "Opt IR lowering emits functions");

    auto [optimized_functions, opt_pass_timing] = measure("opt pass fixpoint", [&]() {
        std::size_t count = 0;
        for (auto &function : opt_program.functions) {
            static_cast<void>(ahfl::ir::opt::optimize(function));
            ++count;
        }
        return count;
    });
    check(optimized_functions == opt_program.functions.size(), "Opt pass visits every function");

    const std::vector<Timing> timings{
        parse_timing,
        resolve_timing,
        typecheck_timing,
        validate_timing,
        lookup_timing,
        lowering_timing,
        visitor_timing,
        opt_lower_timing,
        opt_pass_timing,
    };

    std::printf("\nResults:\n");
    std::printf("  source_bytes: %zu\n", source.size());
    std::printf("  typed_exprs: %zu\n", type_result.typed_program.expressions.size());
    std::printf("  ir_exprs: %zu\n", ir_program.expr_arena.size());
    std::printf("  opt_functions: %zu\n", opt_program.functions.size());
    for (const auto &timing : timings) {
        check(timing.duration.count() >= 0, (timing.name + " duration is non-negative").c_str());
        std::printf("  %-20s %lld us\n",
                    timing.name.c_str(),
                    static_cast<long long>(timing.duration.count()));
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

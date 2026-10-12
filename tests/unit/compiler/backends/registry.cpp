#include "ahfl/compiler/backends/registry.hpp"
#include "ahfl/compiler/backends/driver.hpp"
#include "ahfl/compiler/backends/smv.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/ir.hpp"

#include <cstdio>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name);
    } else {
        std::printf("  FAIL: %s\n", name);
    }
}

struct ExpectedBackend {
    ahfl::BackendKind kind;
    std::string_view name;
};

} // namespace

int main() {
    std::printf("=== Backend Registry Tests ===\n\n");

    constexpr ExpectedBackend expected_backends[] = {
        {ahfl::BackendKind::Ir, "ir"},
        {ahfl::BackendKind::IrJson, "ir-json"},
        {ahfl::BackendKind::CoreIrJson, "core-ir-json"},
        {ahfl::BackendKind::NativeJson, "native-json"},
        {ahfl::BackendKind::ExecutionPlan, "execution-plan"},
        {ahfl::BackendKind::PackageReview, "package-review"},
        {ahfl::BackendKind::Summary, "summary"},
        {ahfl::BackendKind::Smv, "smv"},
        {ahfl::BackendKind::AssuranceJson, "assurance-json"},
    };

    auto &registry = ahfl::global_backend_registry();
    const auto original_size = registry.size();

    for (const auto &expected : expected_backends) {
        const auto *entry = registry.find(expected.kind);
        check(entry != nullptr && entry->name == expected.name, "find built-in backend by kind");
        check(registry.find_by_name(expected.name) == entry, "find built-in backend by name");
    }

    check(registry.size() >= std::size(expected_backends), "registry contains built-in backends");
    check(registry.find_by_name("missing-backend") == nullptr, "missing backend lookup is null");

    ahfl::BackendEntry duplicate{
        ahfl::BackendKind::Ir,
        "duplicate-ir",
        "duplicate IR backend",
        [](const ahfl::EmitContext &) -> ahfl::EmitResult { return {}; },
    };
    check(!registry.register_builtin_backend(std::move(duplicate)),
          "duplicate backend kind is rejected");
    check(registry.size() == original_size, "duplicate registration does not mutate registry");

    ahfl::BackendRegistry local_registry;
    ahfl::BackendEntry local_ir{
        ahfl::BackendKind::Ir,
        "ir",
        "IR backend",
        [](const ahfl::EmitContext &) -> ahfl::EmitResult { return {}; },
    };
    check(local_registry.register_builtin_backend(std::move(local_ir)),
          "local backend registration succeeds");
    ahfl::BackendEntry duplicate_name{
        ahfl::BackendKind::IrJson,
        "ir",
        "duplicate name backend",
        [](const ahfl::EmitContext &) -> ahfl::EmitResult { return {}; },
    };
    check(!local_registry.register_builtin_backend(std::move(duplicate_name)),
          "duplicate backend name is rejected");

    ahfl::ir::Program program;
    std::ostringstream out;
    auto result = ahfl::emit_backend(ahfl::BackendKind::Ir, program, out, nullptr);
    check(result.has_value(), "emit_backend succeeds for registered backend");
    check(program.phase == ahfl::ir::ProgramPhase::Analyzed,
          "emit_backend promotes lowered IR to analyzed phase before emission");
    check(ahfl::ir::has_fresh_derived_analyses(program),
          "emit_backend ensures fresh derived analyses before emission");


    // Anonymous contracts have independent flat-store identity even when an
    // embedding caller leaves both provenance IDs at their default value.
    {
        ahfl::ir::Program multi;
        ahfl::ir::AgentDecl agent;
        agent.name = "review::A";
        agent.symbol_ref = {.kind = ahfl::ir::SymbolRefKind::Agent,
                            .canonical_name = agent.name};
        agent.states = {"Init", "Done"};
        agent.initial_state = "Init";
        agent.final_states = {"Done"};
        multi.declarations.emplace_back(std::move(agent));
        auto first_expr = multi.expr_arena.make(ahfl::ir::BoolLiteralExpr{.value = true});
        auto second_expr = multi.expr_arena.make(ahfl::ir::BoolLiteralExpr{.value = false});
        ahfl::ir::ContractDecl first;
        first.target_ref = {.kind = ahfl::ir::SymbolRefKind::Agent,
                            .canonical_name = "review::A"};
        first.provenance.source_path = "first.ahfl";
        first.clauses.push_back({.kind = ahfl::ir::ContractClauseKind::Requires,
                                 .value = first_expr,
                                 .source_range = ahfl::SourceRange{10, 20}});
        multi.declarations.emplace_back(std::move(first));
        ahfl::ir::ContractDecl second;
        second.target_ref = {.kind = ahfl::ir::SymbolRefKind::Agent,
                             .canonical_name = "review::A"};
        second.provenance.source_path = "second.ahfl";
        second.clauses.push_back({.kind = ahfl::ir::ContractClauseKind::Ensures,
                                  .value = second_expr,
                                  .source_range = ahfl::SourceRange{10, 20}});
        auto embedded = ahfl::Owned<ahfl::ir::TemporalExpr>(new ahfl::ir::TemporalExpr{
            .node = ahfl::ir::EmbeddedTemporalExpr{.expr = second_expr}});
        second.clauses.push_back({
            .kind = ahfl::ir::ContractClauseKind::Invariant,
            .value = ahfl::Owned<ahfl::ir::TemporalExpr>(new ahfl::ir::TemporalExpr{
                .node = ahfl::ir::TemporalUnaryExpr{
                    .op = ahfl::ir::TemporalUnaryOp::Always,
                    .operand = std::move(embedded)}}),
            .source_range = ahfl::SourceRange{21, 30}});
        multi.declarations.emplace_back(std::move(second));
        ahfl::ir::ContractClauseIndex clauses;
        clauses.rebuild(multi);
        check(clauses.clause_begin(std::get<ahfl::ir::ContractDecl>(multi.declarations[2])) == 1,
              "anonymous synthetic contracts receive distinct cumulative ordinals");
        ahfl::ir::recompute_formal_observations(multi);
        const auto observations = ahfl::collect_formal_observations(multi);
        check(observations.size() == 3 && observations[0].symbol != observations[1].symbol,
              "multi-contract observations retain both block-local zeroth clauses");
        std::ostringstream smv;
        ahfl::print_program_smv(multi, smv);
        const auto model = smv.str();
        check(model.find("ensures[1]") == std::string::npos &&
                  model.find("invariant[2]") != std::string::npos &&
                  model.find("LTLSPEC G (FALSE)") != std::string::npos,
              "control SMV excludes data clauses and retains cumulative temporal ordinals");
        check(model.find("invariant[2] @ second.ahfl:21-30") != std::string::npos,
              "SMV violation mapping retains the later temporal contract source");
        const auto package = ahfl::handoff::lower_package(multi);
        const auto *obligation = ahfl::handoff::find_policy_obligation(
            package, "review::A", ahfl::handoff::PolicyObligationKind::Ensures, 1);
        check(obligation != nullptr && obligation->observation_symbols.size() == 1 &&
                  obligation->observation_symbols[0] == observations[1].symbol,
              "handoff later contract obligation maps to its own observation");
        const auto *temporal = ahfl::handoff::find_policy_obligation(
            package, "review::A", ahfl::handoff::PolicyObligationKind::Invariant, 2);
        check(temporal != nullptr && temporal->observation_symbols.size() == 1 &&
                  temporal->observation_symbols[0] == observations[2].symbol,
              "handoff temporal clause maps independently across contract blocks");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

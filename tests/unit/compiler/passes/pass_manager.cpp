#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/passes/pass_manager.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace ahfl::passes;

[[nodiscard]] ahfl::ir::TypeRef unit_type_ref() {
    return ahfl::ir::TypeRef{
        .kind = ahfl::ir::TypeRefKind::Unit,
        .display_name = "Unit",
        .canonical_name = {},
        .string_bounds = {},
        .decimal_scale = {},
        .first = nullptr,
        .second = nullptr,
    };
}

class NoOpPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "NoOpPass";
    }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override {
        return false;
    }
};

class AlwaysModifyPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "AlwaysModifyPass";
    }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override {
        return true;
    }
};

class RequiresFormalObservationsPass final : public Pass {
  public:
    explicit RequiresFormalObservationsPass(bool &saw_fresh) : saw_fresh_(saw_fresh) {}

    [[nodiscard]] std::string_view name() const override {
        return "RequiresFormalObservationsPass";
    }
    [[nodiscard]] std::vector<ahfl::ir::DerivedAnalysisKind>
    required_derived_analyses() const override {
        return {ahfl::ir::DerivedAnalysisKind::FormalObservations};
    }
    [[nodiscard]] bool run(ahfl::ir::Program &program) override {
        const auto required = required_derived_analyses();
        saw_fresh_ = ahfl::ir::has_fresh_derived_analyses(program, required);
        return false;
    }

  private:
    bool &saw_fresh_;
};

struct CountingAnalysisResult final : AnalysisResult {
    explicit CountingAnalysisResult(int count_value) : count(count_value) {}
    int count{0};
};

class CountingAnalysisPass final : public AnalysisPass {
  public:
    explicit CountingAnalysisPass(int &run_count) : run_count_(run_count) {}

    [[nodiscard]] std::string_view name() const override {
        return "counting-analysis";
    }
    [[nodiscard]] std::unique_ptr<AnalysisResult>
    run(const ahfl::ir::Program & /*program*/) override {
        ++run_count_;
        return std::make_unique<CountingAnalysisResult>(run_count_);
    }

  private:
    int &run_count_;
};

class InvalidatesCountingAnalysisPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "InvalidatesCountingAnalysisPass";
    }
    [[nodiscard]] std::vector<std::string_view> required_analyses() const override {
        return {"counting-analysis"};
    }
    [[nodiscard]] std::vector<std::string_view> invalidated_analyses() const override {
        return {"counting-analysis"};
    }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override {
        return true;
    }
};

} // namespace

TEST_CASE("PassManager: empty pipeline") {
    PassManager pm;
    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    CHECK_FALSE(result.any_modified);
    CHECK(result.executed.empty());
    CHECK(result.timings_ms.empty());
}

TEST_CASE("PassManager: no-op pass") {
    PassManager pm;
    pm.add_pass(std::make_unique<NoOpPass>());
    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    CHECK_FALSE(result.any_modified);
    REQUIRE(result.executed.size() == 1);
    CHECK(result.executed[0] == "NoOpPass");
}

TEST_CASE("PassManager: modifying pass") {
    PassManager pm;
    pm.add_pass(std::make_unique<AlwaysModifyPass>());
    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    CHECK(result.any_modified);
    REQUIRE(result.executed.size() == 1);
    CHECK(result.executed[0] == "AlwaysModifyPass");
}

TEST_CASE("PassManager: modifying pass recomputes derived IR analyses") {
    PassManager pm;
    pm.add_pass(std::make_unique<AlwaysModifyPass>());

    ahfl::ir::Program prog;
    prog.declarations.push_back(ahfl::ir::AgentDecl{
        .provenance = {},
        .name = "pkg::Agent",
        .states = {"Init", "Done"},
        .initial_state = "Init",
        .final_states = {"Done"},
        .quota = {},
        .transitions = {},
        .input_type_ref = unit_type_ref(),
        .context_type_ref = unit_type_ref(),
        .output_type_ref = unit_type_ref(),
        .capability_refs =
            {
                ahfl::ir::SymbolRef{
                    .kind = ahfl::ir::SymbolRefKind::Capability,
                    .canonical_name = "pkg::Call",
                    .local_name = "Call",
                    .module_name = "pkg",
                },
            },
        .symbol_ref = {},
    });

    auto result = pm.run(prog);
    CHECK(result.any_modified);
    CHECK(prog.phase == ahfl::ir::ProgramPhase::Optimized);
    CHECK(prog.analysis_revision == 1);
    CHECK(prog.analyses.source_program_revision == prog.analysis_revision);
    CHECK(ahfl::ir::has_fresh_derived_analyses(prog));
    REQUIRE(ahfl::ir::formal_observations(prog).size() == 1);
    CHECK(ahfl::ir::formal_observations(prog)[0].symbol == "agent__pkg_Agent__called__pkg_Call");
}

TEST_CASE("PassManager: required derived analyses are fresh before pass runs") {
    PassManager pm;
    bool saw_fresh = false;
    pm.add_pass(std::make_unique<RequiresFormalObservationsPass>(saw_fresh));

    ahfl::ir::Program prog;
    const std::vector required{ahfl::ir::DerivedAnalysisKind::FormalObservations};
    CHECK_FALSE(ahfl::ir::has_fresh_derived_analyses(prog, required));

    auto result = pm.run(prog);

    CHECK_FALSE(result.any_modified);
    CHECK(saw_fresh);
    CHECK(prog.phase == ahfl::ir::ProgramPhase::Analyzed);
    CHECK(ahfl::ir::has_fresh_derived_analyses(prog, required));
}

TEST_CASE("PassManager: modifying pass invalidates and reruns declared analyses") {
    PassManager pm;
    int run_count = 0;
    pm.add_pass(std::make_unique<InvalidatesCountingAnalysisPass>());
    pm.add_analysis(std::make_unique<CountingAnalysisPass>(run_count));

    ahfl::ir::Program prog;
    auto result = pm.run(prog);

    CHECK(result.any_modified);
    CHECK(run_count == 2);
    REQUIRE(result.executed.size() == 2);
    CHECK(result.executed[0] == "InvalidatesCountingAnalysisPass");
    CHECK(result.executed[1] == "counting-analysis");
}

TEST_CASE("PassManager: default pipeline creation") {
    auto pipeline = create_default_pipeline();
    REQUIRE(pipeline != nullptr);
    auto names = pipeline->registered_pass_names();
    CHECK(names.size() == 4);
    auto analyses = pipeline->registered_analysis_names();
    CHECK(analyses.size() == 2);
}

TEST_CASE("PassManager: pass filter") {
    auto pm = create_default_pipeline();
    ahfl::ir::Program prog;
    pm->set_pass_filter({"dead-state-elimination"});
    auto result = pm->run(prog);
    // Only the filtered pass (or none if name doesn't match exactly) should execute
    for (const auto &name : result.executed) {
        // Analysis passes always run, but transform passes should be filtered
        CHECK((name == "dead-state-elimination" || name == "capability-reachability" ||
               name == "contract-redundancy"));
    }
}

TEST_CASE("PassManager: timing collection") {
    PassManager pm;
    pm.add_pass(std::make_unique<NoOpPass>());
    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    REQUIRE(result.timings_ms.size() == 1);
    CHECK(result.timings_ms[0].first == "NoOpPass");
    CHECK(result.timings_ms[0].second >= 0.0);
}

TEST_CASE("PassManager: run_to_fixpoint") {
    PassManager pm;
    pm.add_pass(std::make_unique<NoOpPass>());
    ahfl::ir::Program prog;
    auto result = pm.run_to_fixpoint(prog, 5);
    // NoOpPass never modifies, so fixpoint exits after first iteration
    CHECK_FALSE(result.any_modified);
    CHECK(result.executed.size() == 1);
}

TEST_CASE("PassManager: registered names") {
    PassManager pm;
    pm.add_pass(std::make_unique<NoOpPass>());
    pm.add_pass(std::make_unique<AlwaysModifyPass>());
    auto names = pm.registered_pass_names();
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "NoOpPass");
    CHECK(names[1] == "AlwaysModifyPass");
}

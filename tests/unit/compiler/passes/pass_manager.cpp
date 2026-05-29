#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "compiler/passes/pass_manager.hpp"
#include "ahfl/compiler/ir/ir.hpp"

#include <memory>
#include <string_view>

namespace {

using namespace ahfl::passes;

class NoOpPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "NoOpPass"; }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override { return false; }
};

class AlwaysModifyPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "AlwaysModifyPass"; }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override { return true; }
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

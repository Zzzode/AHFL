#include "ahfl/passes/pass_manager.hpp"

#include "ahfl/passes/capability_reachability.hpp"
#include "ahfl/passes/contract_redundancy.hpp"
#include "ahfl/passes/dead_state_elimination.hpp"
#include "ahfl/passes/expr_canonicalization.hpp"
#include "ahfl/passes/temporal_simplification.hpp"
#include "ahfl/passes/workflow_simplification.hpp"

#include <string>

namespace ahfl::passes {

void PassManager::add_pass(std::unique_ptr<Pass> pass) { passes_.push_back(std::move(pass)); }

void PassManager::add_analysis(std::unique_ptr<AnalysisPass> analysis) {
    analyses_.push_back(std::move(analysis));
}

PassManager::RunResult PassManager::run(ir::Program &program) {
    RunResult result;

    // Run transformation passes
    for (auto &pass : passes_) {
        bool modified = pass->run(program);
        if (modified) {
            result.any_modified = true;
        }
        result.executed.emplace_back(pass->name());
    }

    // Run analysis passes
    analysis_results_.clear();
    for (auto &analysis : analyses_) {
        auto ar = analysis->run(program);
        result.executed.emplace_back(analysis->name());
        analysis_results_.push_back(std::move(ar));
    }

    return result;
}

std::unique_ptr<PassManager> create_default_pipeline() {
    auto pm = std::make_unique<PassManager>();

    // Transformations
    pm->add_pass(std::make_unique<DeadStateEliminationPass>());
    pm->add_pass(std::make_unique<WorkflowSimplificationPass>());
    pm->add_pass(std::make_unique<ExprCanonicalizationPass>());
    pm->add_pass(std::make_unique<TemporalSimplificationPass>());

    // Analyses
    pm->add_analysis(std::make_unique<CapabilityReachabilityPass>());
    pm->add_analysis(std::make_unique<ContractRedundancyPass>());

    return pm;
}

} // namespace ahfl::passes

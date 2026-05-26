#include "passes/pass_manager.hpp"

#include "passes/capability_reachability.hpp"
#include "passes/contract_redundancy.hpp"
#include "passes/dead_state_elimination.hpp"
#include "passes/expr_canonicalization.hpp"
#include "passes/temporal_simplification.hpp"
#include "passes/workflow_simplification.hpp"

#include <algorithm>
#include <string>

namespace ahfl::passes {

void PassManager::add_pass(std::unique_ptr<Pass> pass) { passes_.push_back(std::move(pass)); }

void PassManager::add_analysis(std::unique_ptr<AnalysisPass> analysis) {
    analyses_.push_back(std::move(analysis));
}

void PassManager::set_pass_filter(std::vector<std::string_view> pass_names) {
    pass_filter_.clear();
    for (auto name : pass_names) {
        pass_filter_.insert(name);
    }
}

std::vector<std::string_view> PassManager::registered_pass_names() const {
    std::vector<std::string_view> names;
    names.reserve(passes_.size());
    for (const auto &pass : passes_) {
        names.push_back(pass->name());
    }
    return names;
}

std::vector<std::string_view> PassManager::registered_analysis_names() const {
    std::vector<std::string_view> names;
    names.reserve(analyses_.size());
    for (const auto &analysis : analyses_) {
        names.push_back(analysis->name());
    }
    return names;
}

void PassManager::ensure_analysis(std::string_view name, const ir::Program &program) {
    // Check if already computed
    for (std::size_t i = 0; i < analyses_.size(); ++i) {
        if (analyses_[i]->name() == name && i < analysis_results_.size() &&
            analysis_results_[i] != nullptr) {
            return;
        }
    }

    // Find and run the analysis
    for (std::size_t i = 0; i < analyses_.size(); ++i) {
        if (analyses_[i]->name() == name) {
            if (i >= analysis_results_.size()) {
                analysis_results_.resize(analyses_.size());
            }
            analysis_results_[i] = analyses_[i]->run(program);
            return;
        }
    }
}

void PassManager::invalidate_analysis(std::string_view name) {
    for (std::size_t i = 0; i < analyses_.size(); ++i) {
        if (analyses_[i]->name() == name && i < analysis_results_.size()) {
            analysis_results_[i].reset();
        }
    }
}

PassManager::RunResult PassManager::run(ir::Program &program) {
    RunResult result;
    analysis_results_.resize(analyses_.size());

    // Run transformation passes
    for (auto &pass : passes_) {
        // Skip if filter is active and this pass is not in the filter
        if (!pass_filter_.empty() && pass_filter_.find(pass->name()) == pass_filter_.end()) {
            continue;
        }

        // Ensure required analyses are available
        for (auto req : pass->required_analyses()) {
            ensure_analysis(req, program);
        }

        bool modified = pass->run(program);
        if (modified) {
            result.any_modified = true;
            // Invalidate analyses declared by this pass
            for (auto inv : pass->invalidated_analyses()) {
                invalidate_analysis(inv);
            }
        }
        result.executed.emplace_back(pass->name());
    }

    // Run analysis passes (only those not yet computed)
    for (std::size_t i = 0; i < analyses_.size(); ++i) {
        if (analysis_results_[i] == nullptr) {
            analysis_results_[i] = analyses_[i]->run(program);
            result.executed.emplace_back(analyses_[i]->name());
        }
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

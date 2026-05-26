#include "passes/pass_manager.hpp"

#include "passes/capability_reachability.hpp"
#include "passes/contract_redundancy.hpp"
#include "passes/dead_state_elimination.hpp"
#include "passes/expr_canonicalization.hpp"
#include "passes/temporal_simplification.hpp"
#include "passes/workflow_simplification.hpp"

#include <algorithm>
#include <chrono>
#include <string>

namespace ahfl::passes {

void PassManager::add_pass(std::unique_ptr<Pass> pass) { passes_.push_back(std::move(pass)); }

void PassManager::add_analysis(std::unique_ptr<AnalysisPass> analysis) {
    analysis_index_[analysis->name()] = analyses_.size();
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
    auto it = analysis_index_.find(name);
    if (it == analysis_index_.end()) {
        return;
    }
    std::size_t idx = it->second;
    if (idx < analysis_results_.size() && analysis_results_[idx] != nullptr) {
        return;
    }
    if (idx >= analysis_results_.size()) {
        analysis_results_.resize(analyses_.size());
    }
    analysis_results_[idx] = analyses_[idx]->run(program);
}

void PassManager::invalidate_analysis(std::string_view name) {
    auto it = analysis_index_.find(name);
    if (it != analysis_index_.end() && it->second < analysis_results_.size()) {
        analysis_results_[it->second].reset();
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

        auto start = std::chrono::steady_clock::now();
        bool modified = pass->run(program);
        auto elapsed = std::chrono::steady_clock::now() - start;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        result.timings_ms.emplace_back(pass->name(), ms);

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

PassManager::RunResult PassManager::run_to_fixpoint(ir::Program &program,
                                                     std::size_t max_iterations) {
    RunResult combined{};
    for (std::size_t i = 0; i < max_iterations; ++i) {
        auto result = run(program);
        combined.executed.insert(combined.executed.end(), result.executed.begin(),
                                 result.executed.end());
        combined.timings_ms.insert(combined.timings_ms.end(), result.timings_ms.begin(),
                                    result.timings_ms.end());
        if (!result.any_modified) {
            break;
        }
        combined.any_modified = true;
    }
    return combined;
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

#include "passes/capability_reachability.hpp"

#include "ahfl/ir/ir.hpp"

#include <string>
#include <unordered_set>
#include <variant>

namespace ahfl::passes {

std::unique_ptr<AnalysisResult> CapabilityReachabilityPass::run(const ir::Program &program) {
    auto result = std::make_unique<CapabilityReachabilityResult>();

    // Collect all capabilities called across all flows
    std::unordered_set<std::string> called_capabilities;
    for (const auto &decl : program.declarations) {
        const auto *flow = std::get_if<ir::FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }
        for (const auto &handler : flow->state_handlers) {
            for (const auto &target : handler.summary.called_targets) {
                called_capabilities.insert(target);
            }
        }
    }

    // Check each agent's declared capabilities against called set
    for (const auto &decl : program.declarations) {
        const auto *agent = std::get_if<ir::AgentDecl>(&decl);
        if (agent == nullptr) {
            continue;
        }
        for (const auto &cap : agent->capabilities) {
            if (called_capabilities.find(cap) == called_capabilities.end()) {
                result->unused.push_back({agent->name, cap});
            }
        }
    }

    return result;
}

} // namespace ahfl::passes

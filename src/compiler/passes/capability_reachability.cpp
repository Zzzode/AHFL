#include "compiler/passes/capability_reachability.hpp"

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/ir.hpp"

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
            const auto *summary = ir::find_state_handler_summary(program, *flow, handler);
            if (summary == nullptr) {
                continue;
            }
            for (const auto &target : summary->called_targets) {
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
        for (const auto &capability_ref : agent->capability_refs) {
            const auto cap = std::string(ir::symbol_canonical_name(capability_ref));
            if (cap.empty()) {
                continue;
            }
            if (called_capabilities.find(cap) == called_capabilities.end()) {
                result->unused.push_back({agent->name, cap});
            }
        }
    }

    return result;
}

} // namespace ahfl::passes

#pragma once

#include "compiler/passes/pass_manager.hpp"

#include <string>
#include <vector>

namespace ahfl::passes {

/// Analysis result: reports capabilities declared but never called.
struct CapabilityReachabilityResult final : public AnalysisResult {
    struct UnusedCapability {
        std::string agent_name;
        std::string capability_name;
    };
    std::vector<UnusedCapability> unused;
};

/// Checks which capabilities declared on agents are actually called in flows.
class CapabilityReachabilityPass final : public AnalysisPass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "capability-reachability";
    }
    [[nodiscard]] std::unique_ptr<AnalysisResult> run(const ir::Program &program) override;
};

} // namespace ahfl::passes

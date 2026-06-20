#pragma once

#include "compiler/passes/pass_manager.hpp"

namespace ahfl::passes {

/// Removes unreachable states and transitions from AgentDecl.
/// A state is unreachable if there's no path from initial_state via transitions.
class DeadStateEliminationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "dead-state-elimination";
    }
    [[nodiscard]] bool run(ir::Program &program) override;
};

} // namespace ahfl::passes

#pragma once

#include "compiler/passes/pass_manager.hpp"

namespace ahfl::passes {

/// Eliminates transitive-redundant `after` edges in WorkflowDecl DAGs.
/// If A->B->C and A->C, the direct A->C edge is redundant.
class WorkflowSimplificationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "workflow-simplification"; }
    [[nodiscard]] bool run(ir::Program &program) override;
};

} // namespace ahfl::passes

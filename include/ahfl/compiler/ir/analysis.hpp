#pragma once

#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

[[nodiscard]] const StateHandler::Summary *
find_state_handler_summary(const Program &program,
                           const FlowDecl &flow,
                           const StateHandler &handler);

[[nodiscard]] const WorkflowExprSummary *
find_workflow_node_input_summary(const Program &program,
                                 const WorkflowDecl &workflow,
                                 const WorkflowNode &node);

[[nodiscard]] const WorkflowExprSummary *
find_workflow_return_summary(const Program &program, const WorkflowDecl &workflow);

[[nodiscard]] const std::vector<FormalObservation> &formal_observations(
    const Program &program) noexcept;

void recompute_state_handler_summaries(Program &program);
void recompute_workflow_expr_summaries(Program &program);
void recompute_formal_observations(Program &program);
void recompute_derived_analyses(Program &program, ProgramPhase phase = ProgramPhase::Analyzed);

} // namespace ahfl::ir

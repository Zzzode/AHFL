#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

enum class DerivedAnalysisKind {
    StateHandlerSummaries,
    WorkflowExprSummaries,
    FormalObservations,
};

[[nodiscard]] std::span<const DerivedAnalysisKind> all_derived_analysis_kinds() noexcept;
[[nodiscard]] std::vector<DerivedAnalysisKind> all_derived_analysis_kinds_vector();

void mark_derived_analyses_stale(Program &program);
[[nodiscard]] bool has_fresh_derived_analyses(
    const Program &program,
    std::span<const DerivedAnalysisKind> required = all_derived_analysis_kinds()) noexcept;
void ensure_derived_analyses(
    Program &program,
    std::span<const DerivedAnalysisKind> required = all_derived_analysis_kinds(),
    ProgramPhase phase = ProgramPhase::Analyzed);

[[nodiscard]] const StateHandler::Summary *find_state_handler_summary(const Program &program,
                                                                      const FlowDecl &flow,
                                                                      const StateHandler &handler);

[[nodiscard]] const WorkflowExprSummary *find_workflow_node_input_summary(
    const Program &program, const WorkflowDecl &workflow, const WorkflowNode &node);

[[nodiscard]] const WorkflowExprSummary *find_workflow_return_summary(const Program &program,
                                                                      const WorkflowDecl &workflow);

[[nodiscard]] const std::vector<FormalObservation> &
formal_observations(const Program &program) noexcept;

void recompute_state_handler_summaries(Program &program);
void recompute_workflow_expr_summaries(Program &program);
void recompute_formal_observations(Program &program);
void recompute_derived_analyses(Program &program, ProgramPhase phase = ProgramPhase::Analyzed);

} // namespace ahfl::ir

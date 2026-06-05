#include "compiler/backends/smv/smv_printer.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

std::string SmvPrinter::wrap_formula_with_observation_assumptions(
    std::string formula, const std::vector<std::string> &observation_assumptions) const {
    std::vector<std::string> unique_assumptions;
    for (const auto &assumption : observation_assumptions) {
        if (std::ranges::find(unique_assumptions, assumption) == unique_assumptions.end()) {
            unique_assumptions.push_back(assumption);
        }
    }

    if (unique_assumptions.empty()) {
        return formula;
    }

    return "(G (" + render_conjunction(unique_assumptions) + ") -> (" + formula + "))";
}

std::string
SmvPrinter::wrap_formula_with_workflow_no_failure_assumption(const ir::WorkflowDecl &workflow,
                                                             std::string formula) const {
    std::vector<std::string> no_failure_guards;
    no_failure_guards.reserve(workflow.nodes.size());

    for (const auto &node : workflow.nodes) {
        no_failure_guards.push_back("!" + workflow_node_failure_requested_var(workflow, node.name));
    }

    if (no_failure_guards.empty()) {
        return formula;
    }

    return "(G (" + render_conjunction(no_failure_guards) + ") -> (" + formula + "))";
}

std::string
SmvPrinter::render_agent_formula(const ir::TemporalExpr &expr,
                                 const ir::AgentDecl &agent,
                                 std::size_t clause_index,
                                 std::size_t &atom_index,
                                 std::vector<std::string> &observation_assumptions) const {
    return std::visit(
        Overloaded{
            [&](const ir::EmbeddedTemporalExpr &value) {
                if (const auto lowered = render_bounded_expr(*value.expr); lowered.has_value()) {
                    return *lowered;
                }

                const auto observation = lookup_embedded_observation_symbol(
                    ir::FormalObservationScopeKind::ContractClause,
                    agent.name,
                    clause_index,
                    atom_index++);
                observation_assumptions.push_back(observation);
                return observation;
            },
            [&](const ir::CalledTemporalExpr &value) {
                return lookup_called_observation_symbol(agent.name, value.capability);
            },
            [&](const ir::InStateTemporalExpr &value) {
                return "(" + agent_state_var(agent) + " = " + value.state + ")";
            },
            [&](const ir::TemporalUnaryExpr &value) {
                return smv_unary_op(value.op) + " (" +
                       render_agent_formula(*value.operand,
                                            agent,
                                            clause_index,
                                            atom_index,
                                            observation_assumptions) +
                       ")";
            },
            [&](const ir::TemporalBinaryExpr &value) {
                return "(" +
                       render_agent_formula(
                           *value.lhs, agent, clause_index, atom_index, observation_assumptions) +
                       " " + smv_binary_op(value.op) + " " +
                       render_agent_formula(
                           *value.rhs, agent, clause_index, atom_index, observation_assumptions) +
                       ")";
            },
            [&](const auto &) { return std::string("FALSE"); },
        },
        expr.node);
}

std::optional<std::string> SmvPrinter::render_contract_expr_clause(const ir::ContractDecl &contract,
                                                                   const ir::AgentDecl &agent,
                                                                   ir::ContractClauseKind kind,
                                                                   const ir::Expr &expr,
                                                                   std::size_t clause_index) {
    const auto target = std::string(ir::symbol_canonical_name(contract.target_ref));
    const auto bounded_expr = render_bounded_expr(expr);
    if (bounded_expr.has_value()) {
        switch (kind) {
        case ir::ContractClauseKind::Requires:
            specs_.push_back("-- contract " + target + " requires[" + std::to_string(clause_index) +
                             "] bounded_precondition_assumption: " + *bounded_expr);
            return std::nullopt;
        case ir::ContractClauseKind::Ensures: {
            const auto final_guard =
                render_state_membership(agent_state_var(agent), agent.final_states);
            return "G ((" + final_guard + ") -> (" + *bounded_expr + "))";
        }
        case ir::ContractClauseKind::Invariant:
            return "G (" + *bounded_expr + ")";
        case ir::ContractClauseKind::Forbid:
            return "G (!(" + *bounded_expr + "))";
        }
    }

    const auto observation = lookup_embedded_observation_symbol(
        ir::FormalObservationScopeKind::ContractClause, target, clause_index, 0);

    switch (kind) {
    case ir::ContractClauseKind::Requires:
        specs_.push_back("-- contract " + target + " requires[" + std::to_string(clause_index) +
                         "] observation_assumption: " + observation);
        return std::nullopt;
    case ir::ContractClauseKind::Ensures:
        specs_.push_back("-- contract " + target + " ensures[" + std::to_string(clause_index) +
                         "] data_guarantee_abstracted: " + observation);
        return std::nullopt;
    case ir::ContractClauseKind::Invariant:
    case ir::ContractClauseKind::Forbid:
        specs_.push_back("-- contract " + target + " " + contract_clause_kind_name(kind) + "[" +
                         std::to_string(clause_index) + "] observation_assumption: " + observation);
        return std::nullopt;
    }

    return std::nullopt;
}

std::string
SmvPrinter::render_workflow_formula(const ir::TemporalExpr &expr,
                                    const ir::WorkflowDecl &workflow,
                                    ir::FormalObservationScopeKind scope_kind,
                                    std::size_t clause_index,
                                    std::size_t &atom_index,
                                    std::vector<std::string> &observation_assumptions) const {
    return std::visit(Overloaded{
                          [&](const ir::EmbeddedTemporalExpr &value) {
                              if (const auto lowered = render_bounded_expr(*value.expr);
                                  lowered.has_value()) {
                                  return *lowered;
                              }

                              const auto observation = lookup_embedded_observation_symbol(
                                  scope_kind, workflow.name, clause_index, atom_index++);
                              observation_assumptions.push_back(observation);
                              return observation;
                          },
                          [&](const ir::RunningTemporalExpr &value) {
                              return workflow_node_running_var(workflow, value.node);
                          },
                          [&](const ir::CompletedTemporalExpr &value) {
                              if (!value.state_name.has_value()) {
                                  return workflow_node_completed_var(workflow, value.node);
                              }

                              return "(" + workflow_node_completed_var(workflow, value.node) +
                                     " & (" + workflow_node_state_var(workflow, value.node) +
                                     " = " + *value.state_name + "))";
                          },
                          [&](const ir::TemporalUnaryExpr &value) {
                              return smv_unary_op(value.op) + " (" +
                                     render_workflow_formula(*value.operand,
                                                             workflow,
                                                             scope_kind,
                                                             clause_index,
                                                             atom_index,
                                                             observation_assumptions) +
                                     ")";
                          },
                          [&](const ir::TemporalBinaryExpr &value) {
                              return "(" +
                                     render_workflow_formula(*value.lhs,
                                                             workflow,
                                                             scope_kind,
                                                             clause_index,
                                                             atom_index,
                                                             observation_assumptions) +
                                     " " + smv_binary_op(value.op) + " " +
                                     render_workflow_formula(*value.rhs,
                                                             workflow,
                                                             scope_kind,
                                                             clause_index,
                                                             atom_index,
                                                             observation_assumptions) +
                                     ")";
                          },
                          [&](const auto &) { return std::string("FALSE"); },
                      },
                      expr.node);
}

} // namespace ahfl

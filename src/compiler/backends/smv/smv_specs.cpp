#include "compiler/backends/smv/smv_printer.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

void SmvPrinter::collect_specs() {
    for (const auto &contract : contracts_) {
        const auto contract_target =
            std::string(ir::symbol_canonical_name(contract.get().target_ref));
        const auto agent = find_agent(contract_target);
        if (!agent.has_value()) {
            continue;
        }

        for (std::size_t index = 0; index < contract.get().clauses.size(); ++index) {
            const auto &clause = contract.get().clauses[index];
            std::optional<std::string> formula;
            if (const auto expr = std::get_if<ir::ExprPtr>(&clause.value); expr != nullptr) {
                formula = render_contract_expr_clause(
                    contract.get(), agent->get(), clause.kind, **expr, index);
            } else if (const auto temporal = std::get_if<ir::TemporalExprPtr>(&clause.value);
                       temporal != nullptr) {
                std::size_t atom_index = 0;
                std::vector<std::string> observation_assumptions;
                formula = wrap_formula_with_observation_assumptions(
                    render_agent_formula(
                        **temporal, agent->get(), index, atom_index, observation_assumptions),
                    observation_assumptions);
            } else {
                continue;
            }

            if (!formula.has_value()) {
                continue;
            }

            specs_.push_back("-- contract " + contract_target + " " +
                             contract_clause_kind_name(clause.kind) + "[" + std::to_string(index) +
                             "]");
            specs_.push_back("LTLSPEC " + *formula);
        }
    }

    for (const auto &workflow : workflows_) {
        const auto node_map = build_workflow_node_map(workflow.get());
        collect_workflow_control_specs(workflow.get(), node_map);

        for (std::size_t index = 0; index < workflow.get().safety.size(); ++index) {
            std::size_t atom_index = 0;
            std::vector<std::string> observation_assumptions;
            const auto formula = wrap_formula_with_observation_assumptions(
                render_workflow_formula(*workflow.get().safety[index],
                                        workflow.get(),
                                        ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                        index,
                                        atom_index,
                                        observation_assumptions),
                observation_assumptions);
            specs_.push_back("-- workflow " + workflow.get().name + " safety[" +
                             std::to_string(index) + "]");
            specs_.push_back("LTLSPEC " + wrap_formula_with_workflow_no_failure_assumption(
                                              workflow.get(), formula));
        }

        for (std::size_t index = 0; index < workflow.get().liveness.size(); ++index) {
            std::size_t atom_index = 0;
            std::vector<std::string> observation_assumptions;
            const auto formula = wrap_formula_with_observation_assumptions(
                render_workflow_formula(*workflow.get().liveness[index],
                                        workflow.get(),
                                        ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                        index,
                                        atom_index,
                                        observation_assumptions),
                observation_assumptions);
            specs_.push_back("-- workflow " + workflow.get().name + " liveness[" +
                             std::to_string(index) + "]");
            specs_.push_back("LTLSPEC " + wrap_formula_with_workflow_no_failure_assumption(
                                              workflow.get(), formula));
        }
    }

    collect_effect_obligation_specs();
}

void SmvPrinter::collect_workflow_control_specs(
    const ir::WorkflowDecl &workflow,
    const std::unordered_map<std::string, SmvWorkflowNodeInfo> &node_map) {
    for (const auto &node : workflow.nodes) {
        const auto target = node_map.find(node.name);
        if (target == node_map.end()) {
            continue;
        }

        const auto state_var = workflow_node_state_var(workflow, node.name);
        const auto completed_var = workflow_node_completed_var(workflow, node.name);
        const auto final_guard =
            render_state_membership(state_var, target->second.agent.get().final_states);

        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " lifecycle[completed_final]");
        specs_.push_back("LTLSPEC G (" + completed_var + " -> " + final_guard + ")");

        if (!node.after.empty()) {
            const auto dependency_guard = render_dependency_guard(workflow, node.after);
            specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                             " dependency[start_after]");
            specs_.push_back("LTLSPEC G ((" + workflow_node_running_var(workflow, node.name) +
                             " | " + completed_var + ") -> (" + dependency_guard + "))");
        }

        const auto phase_var = workflow_node_phase_var(workflow, node.name);
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[failed_enters_recovering]");
        specs_.push_back("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeFailed) +
                         ") -> F (" + phase_var + " = " + std::string(kNodeRecovering) + "))");
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[recovering_enters_recovered]");
        specs_.push_back("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeRecovering) +
                         ") -> F (" + phase_var + " = " + std::string(kNodeRecovered) + "))");
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[recovered_resumes_running]");
        specs_.push_back("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeRecovered) +
                         ") -> F (" + workflow_node_running_var(workflow, node.name) + " | " +
                         completed_var + "))");
    }
}

void SmvPrinter::add_effect_obligation_spec(const ir::WorkflowDecl &workflow,
                                            const ir::WorkflowNode &node,
                                            std::string_view called_target,
                                            std::string_view event_symbol,
                                            std::string_view obligation_name,
                                            bool satisfied) {
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " call " +
                     std::string(called_target) + " " + std::string(obligation_name));
    specs_.push_back("LTLSPEC G (" + std::string(event_symbol) + " -> " +
                     std::string(satisfied ? "TRUE" : "FALSE") + ")");
}

void SmvPrinter::add_event_lifecycle_specs(const ir::WorkflowDecl &workflow,
                                           const ir::WorkflowNode &node,
                                           std::string_view event_name,
                                           std::string_view event_symbol,
                                           std::string_view committed_symbol,
                                           std::string_view failed_symbol) {
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " " +
                     std::string(event_name) + " lifecycle[committed_implies_started]");
    specs_.push_back("LTLSPEC G (" + std::string(committed_symbol) + " -> " +
                     std::string(event_symbol) + ")");
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " " +
                     std::string(event_name) + " lifecycle[failed_implies_started]");
    specs_.push_back("LTLSPEC G (" + std::string(failed_symbol) + " -> " +
                     std::string(event_symbol) + ")");
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " " +
                     std::string(event_name) + " lifecycle[commit_fail_exclusive]");
    specs_.push_back("LTLSPEC G (!(" + std::string(committed_symbol) + " & " +
                     std::string(failed_symbol) + "))");
}

void SmvPrinter::add_failed_effect_recovery_spec(const ir::WorkflowDecl &workflow,
                                                 const ir::WorkflowNode &node,
                                                 std::string_view failed_symbol) {
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                     " recovery[failed_effect_reaches_recovery]");
    specs_.push_back("LTLSPEC G (" + std::string(failed_symbol) + " -> F (" +
                     workflow_node_recovering_var(workflow, node.name) + " | " +
                     workflow_node_recovered_var(workflow, node.name) + " | " +
                     workflow_node_running_var(workflow, node.name) + " | " +
                     workflow_node_completed_var(workflow, node.name) + "))");
}

void SmvPrinter::collect_effect_obligation_specs() {
    for (const auto &workflow : workflows_) {
        for (const auto &node : workflow.get().nodes) {
            const auto flow = find_flow(ir::symbol_canonical_name(node.target_ref));
            if (!flow.has_value()) {
                continue;
            }

            for (const auto &handler : flow->get().state_handlers) {
                const auto *summary =
                    program_ == nullptr
                        ? nullptr
                        : ir::find_state_handler_summary(*program_, flow->get(), handler);
                if (summary == nullptr) {
                    continue;
                }

                for (const auto &called_target : summary->called_targets) {
                    const auto call_event =
                        workflow_node_call_event_var(workflow.get(), node.name, called_target);
                    const auto call_committed = workflow_node_call_committed_event_var(
                        workflow.get(), node.name, called_target);
                    const auto call_failed = workflow_node_call_failed_event_var(
                        workflow.get(), node.name, called_target);
                    const auto capability = find_capability(called_target);
                    if (!capability.has_value()) {
                        continue;
                    }

                    const auto &effect = capability->get().effect;
                    const auto effect_kind = capability_effect_kind_name(effect.kind);
                    const auto effect_event =
                        workflow_node_effect_event_var(workflow.get(), node.name, effect_kind);
                    const auto effect_committed = workflow_node_effect_committed_event_var(
                        workflow.get(), node.name, effect_kind);
                    const auto effect_failed = workflow_node_effect_failed_event_var(
                        workflow.get(), node.name, effect_kind);

                    add_event_lifecycle_specs(workflow.get(),
                                              node,
                                              std::string("call ") + called_target,
                                              call_event,
                                              call_committed,
                                              call_failed);
                    add_event_lifecycle_specs(workflow.get(),
                                              node,
                                              std::string("effect ") + effect_kind,
                                              effect_event,
                                              effect_committed,
                                              effect_failed);

                    if (!effect.declared || effect.kind == ir::CapabilityEffectKind::Unknown) {
                        specs_.push_back("-- workflow " + workflow.get().name + " node " +
                                         node.name + " call " + called_target +
                                         " effect[profile_missing_or_unknown]: "
                                         "validate-assurance gate");
                        continue;
                    }

                    add_effect_obligation_spec(
                        workflow.get(), node, called_target, call_event, "effect[declared]", true);

                    if (is_external_or_stronger(effect.kind)) {
                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "effect[audit_event]",
                                                   has_policy(effect, "audit_event"));
                    }

                    if (is_durable_or_stronger(effect.kind)) {
                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "effect[idempotency_key]",
                                                   effect.idempotency_key.has_value());
                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "effect[provider_receipt]",
                                                   effect.receipt_mode ==
                                                       ir::CapabilityReceiptMode::Required);
                        add_effect_obligation_spec(
                            workflow.get(),
                            node,
                            called_target,
                            call_event,
                            "recovery[retry_is_idempotent]",
                            effect.retry_mode != ir::CapabilityRetryMode::SafeIfIdempotent ||
                                effect.idempotency_key.has_value());
                        add_failed_effect_recovery_spec(workflow.get(), node, effect_failed);
                    }

                    if (effect.kind == ir::CapabilityEffectKind::FinancialWrite) {
                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "effect[approval_policy]",
                                                   has_policy(effect, "approval_required"));
                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "recovery[compensation]",
                                                   effect.compensation.has_value());
                    }
                }
            }
        }
    }
}

} // namespace ahfl

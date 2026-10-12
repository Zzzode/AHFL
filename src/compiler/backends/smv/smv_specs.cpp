#include "compiler/backends/smv/smv_printer.hpp"

#include <set>

namespace ahfl {

using namespace smv;
using namespace smv_detail;

namespace {

/// Walk a workflow temporal clause and collect (a) every workflow node it
/// references, and (b) the subset referenced via `completed(node, FinalState)`
/// whose agent declares more than one final state.
void collect_workflow_clause_nodes(
    const ir::TemporalExpr &expr,
    const std::unordered_map<std::string, SmvWorkflowNodeInfo> &node_map,
    std::set<std::string> &all_nodes,
    std::set<std::string> &multifinal_specific_nodes) {
    std::visit(
        Overloaded{
            [&](const ir::CompletedTemporalExpr &value) {
                all_nodes.insert(value.node);
                if (!value.state_name.has_value()) {
                    return;
                }
                const auto target = node_map.find(value.node);
                if (target != node_map.end() &&
                    target->second.agent.get().final_states.size() > 1) {
                    multifinal_specific_nodes.insert(value.node);
                }
            },
            [&](const ir::RunningTemporalExpr &value) { all_nodes.insert(value.node); },
            [&](const ir::TemporalUnaryExpr &value) {
                collect_workflow_clause_nodes(
                    *value.operand, node_map, all_nodes, multifinal_specific_nodes);
            },
            [&](const ir::TemporalBinaryExpr &value) {
                collect_workflow_clause_nodes(
                    *value.lhs, node_map, all_nodes, multifinal_specific_nodes);
                collect_workflow_clause_nodes(
                    *value.rhs, node_map, all_nodes, multifinal_specific_nodes);
            },
            [](const auto &) {},
        },
        expr.node);
}

} // namespace

bool SmvPrinter::workflow_safety_clause_outside_smv_subset(
    const ir::TemporalExpr &expr,
    const std::unordered_map<std::string, SmvWorkflowNodeInfo> &node_map) const {
    std::set<std::string> all_nodes;
    std::set<std::string> multifinal_specific_nodes;
    collect_workflow_clause_nodes(expr, node_map, all_nodes, multifinal_specific_nodes);

    // The SMV model encodes each multi-final-state agent's terminal selection
    // as a nondeterministic choice (the data guard on the outgoing transition
    // is abstracted). A safety clause that correlates one node's specific final
    // state with a *different* node's state therefore admits spurious
    // counterexamples: the model lets the two nodes choose their final states
    // independently. Skip such clauses with a warning instead of emitting a
    // failing spec (sound: no spurious counterexample; the property stays in
    // the source and becomes provable once the model encodes transition
    // guards).
    return !multifinal_specific_nodes.empty() && all_nodes.size() > 1;
}

void SmvPrinter::collect_specs() {
    collect_agent_control_specs();
    for (const auto &contract : contracts_) {
        const auto contract_target =
            std::string(ir::symbol_canonical_name(contract.get().target_ref));
        const auto agent = find_agent(contract_target);
        if (!agent.has_value()) {
            continue;
        }

        for (std::size_t index = 0; index < contract.get().clauses.size(); ++index) {
            const auto &clause = contract.get().clauses[index];
            const auto ordinal = contract_clause_index_.clause_begin(contract.get()) + index;
            std::optional<std::string> formula;
            auto coverage = SmvSpecificationCoverage::Complete;
            if (const auto expr = std::get_if<ir::ExprRef>(&clause.value); expr != nullptr && expr->has_value()) {
                // Data contracts are checked by the opt-in SMT path, never by
                // an unrelated nondeterministic SMV observation.
                continue;
            } else if (const auto temporal = std::get_if<ir::TemporalExprPtr>(&clause.value);
                       temporal != nullptr) {
                std::size_t atom_index = 0;
                std::vector<std::string> observation_assumptions;
                const auto rendered = render_agent_formula(
                    **temporal, agent->get(), ordinal, atom_index, observation_assumptions);
                formula = wrap_formula_with_observation_assumptions(rendered, observation_assumptions);
                if (!observation_assumptions.empty()) {
                    coverage = SmvSpecificationCoverage::Conditional;
                    model_.environment_assumption_count += observation_assumptions.size();
                }
            } else {
                continue;
            }

            if (!formula.has_value()) {
                continue;
            }

            specs_.push_back("-- contract " + contract_target + " " +
                             std::string(ir::contract_clause_kind_name(clause.kind)) + "[" +
                             std::to_string(ordinal) + "]");
            add_spec("LTLSPEC " + *formula, SmvSpecificationOrigin::UserTemporal, coverage,
                     "contract " + contract_target + " " +
                     std::string(ir::contract_clause_kind_name(clause.kind)) + "[" +
                     std::to_string(ordinal) + "]",
                     coverage == SmvSpecificationCoverage::Conditional
                         ? SmvSpecificationAssumption::DataObservation
                         : SmvSpecificationAssumption::None);
            // (P2 §3.5) Map the emitted LTL formula back to the contract clause
            // source range.  The counterexample parser joins the violated spec
            // (echoed verbatim by the checker) against this AHFL_MAP entry to
            // attribute the violation to its AHFL location.  The formula uses
            // `->`/`<->` operators, never ` => `, so it is a safe AHFL_MAP key.
            add_symbol_mapping(*formula,
                               with_source("contract " + contract_target + " " +
                                               std::string(ir::contract_clause_kind_name(
                                                   clause.kind)) +
                                               "[" + std::to_string(ordinal) + "]",
                                           contract.get().provenance.source_path,
                                           clause.source_range));
        }
    }

    for (const auto &workflow : workflows_) {
        const auto node_map = build_workflow_node_map(workflow.get());
        collect_workflow_control_specs(workflow.get(), node_map);

        for (std::size_t index = 0; index < workflow.get().safety.size(); ++index) {
            if (workflow_safety_clause_outside_smv_subset(*workflow.get().safety[index],
                                                          node_map)) {
                specs_.push_back("-- workflow " + workflow.get().name + " safety[" +
                                 std::to_string(index) +
                                 "] outside_smv_subset: correlates a specific final "
                                 "state of a multi-final-state node with another "
                                 "node; the SMV model selects final states "
                                 "nondeterministically and cannot prove cross-node "
                                 "final-state correlations");
                model_.specifications.push_back({model_.specifications.size(),
                    SmvSpecificationOrigin::UserTemporal, SmvSpecificationCoverage::Unsupported,
                    false, "workflow " + workflow.get().name + " safety[" + std::to_string(index) +
                    "] cross-node final-state correlation is outside the SMV subset"});
                continue;
            }
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
            const auto safety_spec =
                wrap_formula_with_workflow_no_failure_assumption(workflow.get(), formula);
            const bool environmental = !workflow.get().nodes.empty();
            model_.environment_assumption_count += observation_assumptions.size() + (environmental ? 1 : 0);
            add_spec("LTLSPEC " + safety_spec, SmvSpecificationOrigin::UserTemporal,
                     environmental || !observation_assumptions.empty()
                         ? SmvSpecificationCoverage::Conditional : SmvSpecificationCoverage::Complete,
                     "workflow " + workflow.get().name + " safety[" + std::to_string(index) +
                     "]",
                     !observation_assumptions.empty() ? SmvSpecificationAssumption::DataObservation
                         : environmental ? SmvSpecificationAssumption::ControlEnvironment
                                         : SmvSpecificationAssumption::None);
            add_symbol_mapping(safety_spec,
                               with_source("workflow " + workflow.get().name + " safety[" +
                                               std::to_string(index) + "]",
                                           workflow.get().provenance.source_path,
                                           workflow.get().safety[index]->source_range));
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
            const auto liveness_spec =
                wrap_formula_with_workflow_no_failure_assumption(workflow.get(), formula);
            const bool environmental = !workflow.get().nodes.empty();
            model_.environment_assumption_count += observation_assumptions.size() + (environmental ? 1 : 0);
            add_spec("LTLSPEC " + liveness_spec, SmvSpecificationOrigin::UserTemporal,
                     environmental || !observation_assumptions.empty()
                         ? SmvSpecificationCoverage::Conditional : SmvSpecificationCoverage::Complete,
                     "workflow " + workflow.get().name + " liveness[" + std::to_string(index) +
                     "]",
                     !observation_assumptions.empty() ? SmvSpecificationAssumption::DataObservation
                         : environmental ? SmvSpecificationAssumption::ControlEnvironment
                                         : SmvSpecificationAssumption::None);
            add_symbol_mapping(liveness_spec,
                               with_source("workflow " + workflow.get().name + " liveness[" +
                                               std::to_string(index) + "]",
                                           workflow.get().provenance.source_path,
                                           workflow.get().liveness[index]->source_range));
        }
    }

    collect_effect_obligation_specs();
}

void SmvPrinter::collect_agent_control_specs() {
    for (const auto *agent : index_.agents()) {
        const auto state_var = agent_state_var(*agent);
        add_spec("LTLSPEC " + state_var + " = " + agent->initial_state,
                 SmvSpecificationOrigin::GeneratedControl, SmvSpecificationCoverage::Complete,
                 "agent " + agent->name + " initial state");
        for (const auto &state : agent->states) {
            std::vector<std::string> successors{state};
            for (const auto &transition : agent->transitions) {
                if (transition.from_state == state &&
                    std::ranges::find(successors, transition.to_state) == successors.end()) {
                    successors.push_back(transition.to_state);
                }
            }
            add_spec("LTLSPEC G ((" + state_var + " = " + state + ") -> X (" +
                     render_state_membership(state_var, successors) + "))",
                     SmvSpecificationOrigin::GeneratedControl, SmvSpecificationCoverage::Complete,
                     "agent " + agent->name + " legal successor of " + state);
        }
        for (const auto &state : agent->final_states) {
            add_spec("LTLSPEC G ((" + state_var + " = " + state + ") -> X (" +
                     state_var + " = " + state + "))",
                     SmvSpecificationOrigin::GeneratedControl, SmvSpecificationCoverage::Complete,
                     "agent " + agent->name + " final state " + state + " is absorbing");
        }
    }
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
        add_spec("LTLSPEC G (" + completed_var + " -> " + final_guard + ")");

        if (!node.after.empty()) {
            const auto dependency_guard = render_dependency_guard(workflow, node.after);
            specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                             " dependency[start_after]");
            add_spec("LTLSPEC G ((" + workflow_node_running_var(workflow, node.name) +
                             " | " + completed_var + ") -> (" + dependency_guard + "))");
        }

        const auto phase_var = workflow_node_phase_var(workflow, node.name);
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[failed_enters_recovering]");
        add_spec("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeFailed) +
                         ") -> F (" + phase_var + " = " + std::string(kNodeRecovering) + "))");
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[recovering_enters_recovered]");
        add_spec("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeRecovering) +
                         ") -> F (" + phase_var + " = " + std::string(kNodeRecovered) + "))");
        specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                         " recovery[recovered_resumes_running]");
        add_spec("LTLSPEC G ((" + phase_var + " = " + std::string(kNodeRecovered) +
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
    add_spec("LTLSPEC G (" + std::string(event_symbol) + " -> " +
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
    add_spec("LTLSPEC G (" + std::string(committed_symbol) + " -> " +
                     std::string(event_symbol) + ")");
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " " +
                     std::string(event_name) + " lifecycle[failed_implies_started]");
    add_spec("LTLSPEC G (" + std::string(failed_symbol) + " -> " +
                     std::string(event_symbol) + ")");
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name + " " +
                     std::string(event_name) + " lifecycle[commit_fail_exclusive]");
    add_spec("LTLSPEC G (!(" + std::string(committed_symbol) + " & " +
                     std::string(failed_symbol) + "))");
}

void SmvPrinter::add_failed_effect_recovery_spec(const ir::WorkflowDecl &workflow,
                                                 const ir::WorkflowNode &node,
                                                 std::string_view failed_symbol) {
    specs_.push_back("-- workflow " + workflow.name + " node " + node.name +
                     " recovery[failed_effect_reaches_recovery]");
    add_spec("LTLSPEC G (" + std::string(failed_symbol) + " -> F (" +
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

                    if (is_effect_pure(summary->inferred_effect)) {
                        add_spec(
                            "LTLSPEC FALSE -- typed effect missing for capability call " +
                            called_target);
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

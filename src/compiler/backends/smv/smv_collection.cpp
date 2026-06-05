#include "compiler/backends/smv/smv_printer.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

void SmvPrinter::collect_state_variables() {
    for (const auto *agent : index_.agents()) {
        if (agent_has_contract(*agent)) {
            const auto state_var = agent_state_var(*agent);
            state_variables_.push_back(state_var + " : {" + join(agent->states, ", ") + "};");
            add_symbol_mapping(state_var,
                               with_source("agent " + agent->name + " state",
                                           agent->provenance.source_path,
                                           agent->provenance.source_range));
        }
    }

    for (const auto &workflow : workflows_) {
        for (const auto &node : workflow.get().nodes) {
            const auto target = std::string(ir::symbol_canonical_name(node.target_ref));
            const auto agent = find_agent(target);
            if (!agent.has_value()) {
                continue;
            }

            const auto state_var = workflow_node_state_var(workflow.get(), node.name);
            const auto phase_var = workflow_node_phase_var(workflow.get(), node.name);
            const auto failure_requested_var =
                workflow_node_failure_requested_var(workflow.get(), node.name);
            state_variables_.push_back(state_var + " : {" + join(agent->get().states, ", ") + "};");
            state_variables_.push_back(
                phase_var + " : {" + std::string(kNodeIdle) + ", " + std::string(kNodeRunning) +
                ", " + std::string(kNodeCompleted) + ", " + std::string(kNodeFailed) + ", " +
                std::string(kNodeRecovering) + ", " + std::string(kNodeRecovered) + ", " +
                std::string(kNodeCompensating) + ", " + std::string(kNodeCompensated) + ", " +
                std::string(kNodeTerminalFailed) + "};");
            add_symbol_mapping(
                state_var,
                with_source("workflow " + workflow.get().name + " node " + node.name + " state",
                            workflow.get().provenance.source_path,
                            node.source_range));
            add_symbol_mapping(phase_var,
                               with_source("workflow " + workflow.get().name + " node " +
                                               node.name + " lifecycle phase",
                                           workflow.get().provenance.source_path,
                                           node.source_range));
            observation_variables_.push_back(failure_requested_var + " : boolean;");
            add_symbol_mapping(failure_requested_var,
                               with_source("workflow " + workflow.get().name + " node " +
                                               node.name + " environment failure request",
                                           workflow.get().provenance.source_path,
                                           node.source_range));
        }
    }
}

void SmvPrinter::collect_defines() {
    for (const auto &workflow : workflows_) {
        const auto node_map = build_workflow_node_map(workflow.get());
        for (const auto &node : workflow.get().nodes) {
            const auto started_name = workflow_node_started_var(workflow.get(), node.name);
            const auto running_name = workflow_node_running_var(workflow.get(), node.name);
            const auto completed_name = workflow_node_completed_var(workflow.get(), node.name);
            const auto failed_name = workflow_node_failed_var(workflow.get(), node.name);
            const auto recovering_name = workflow_node_recovering_var(workflow.get(), node.name);
            const auto recovered_name = workflow_node_recovered_var(workflow.get(), node.name);
            const auto target = node_map.find(node.name);
            if (target == node_map.end()) {
                continue;
            }

            const auto phase_name = workflow_node_phase_var(workflow.get(), node.name);
            const auto final_guard =
                render_state_membership(workflow_node_state_var(workflow.get(), node.name),
                                        target->second.agent.get().final_states);

            defines_.push_back(started_name + " := !(" + phase_name + " = " +
                               std::string(kNodeIdle) + ");");
            defines_.push_back(completed_name + " := (" + phase_name + " = " +
                               std::string(kNodeCompleted) + " | (" + phase_name + " = " +
                               std::string(kNodeRunning) + " & " + final_guard + "));");
            defines_.push_back(running_name + " := (" + phase_name + " = " +
                               std::string(kNodeRunning) + " & !(" + final_guard + "));");
            defines_.push_back(failed_name + " := (" + phase_name + " = " +
                               std::string(kNodeFailed) + " | " + phase_name + " = " +
                               std::string(kNodeTerminalFailed) + ");");
            defines_.push_back(recovering_name + " := (" + phase_name + " = " +
                               std::string(kNodeRecovering) + " | " + phase_name + " = " +
                               std::string(kNodeCompensating) + ");");
            defines_.push_back(recovered_name + " := (" + phase_name + " = " +
                               std::string(kNodeRecovered) + " | " + phase_name + " = " +
                               std::string(kNodeCompensated) + ");");
            add_symbol_mapping(started_name,
                               with_source("workflow " + workflow.get().name + " node " +
                                               node.name + " has started",
                                           workflow.get().provenance.source_path,
                                           node.source_range));
            add_symbol_mapping(running_name,
                               with_source("workflow " + workflow.get().name + " node " +
                                               node.name + " is running",
                                           workflow.get().provenance.source_path,
                                           node.source_range));
            add_symbol_mapping(
                completed_name,
                with_source("workflow " + workflow.get().name + " node " + node.name + " completed",
                            workflow.get().provenance.source_path,
                            node.source_range));
            add_symbol_mapping(
                failed_name,
                with_source("workflow " + workflow.get().name + " node " + node.name + " failed",
                            workflow.get().provenance.source_path,
                            node.source_range));
            add_symbol_mapping(recovering_name,
                               with_source("workflow " + workflow.get().name + " node " +
                                               node.name + " recovering",
                                           workflow.get().provenance.source_path,
                                           node.source_range));
            add_symbol_mapping(
                recovered_name,
                with_source("workflow " + workflow.get().name + " node " + node.name + " recovered",
                            workflow.get().provenance.source_path,
                            node.source_range));
        }
    }

    collect_call_and_effect_event_defines();
    collect_called_observation_defines();
}

void SmvPrinter::collect_assignments() {
    for (const auto *agent : index_.agents()) {
        if (agent_has_contract(*agent)) {
            collect_state_machine_assignments(
                *agent, agent_state_var(*agent), build_effective_adjacency(*agent));
        }
    }

    for (const auto &workflow : workflows_) {
        const auto node_map = build_workflow_node_map(workflow.get());
        for (const auto &node : workflow.get().nodes) {
            const auto target = node_map.find(node.name);
            if (target == node_map.end()) {
                continue;
            }

            collect_workflow_node_assignments(workflow.get(), node, target->second.agent.get());
        }
    }
}

void SmvPrinter::collect_call_and_effect_event_defines() {
    std::map<std::string, std::vector<std::string>> call_guards;
    std::map<std::string, std::vector<std::string>> effect_guards;
    std::map<std::string, std::string> event_failure_vars;
    std::map<std::string, std::string> call_mappings;
    std::map<std::string, std::string> effect_mappings;

    for (const auto &workflow : workflows_) {
        for (const auto &node : workflow.get().nodes) {
            const auto target = std::string(ir::symbol_canonical_name(node.target_ref));
            const auto flow = find_flow(target);
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
                const auto guard = render_workflow_node_state_guard(workflow.get(), node, handler);
                const auto failure_var =
                    workflow_node_failure_requested_var(workflow.get(), node.name);
                const auto source =
                    source_suffix(flow->get().provenance.source_path, handler.source_range);
                for (const auto &called_target : summary->called_targets) {
                    const auto capability = find_capability(called_target);
                    if (!capability.has_value()) {
                        continue;
                    }

                    const auto call_symbol =
                        workflow_node_call_event_var(workflow.get(), node.name, called_target);
                    call_guards[call_symbol].push_back(guard);
                    event_failure_vars.emplace(call_symbol, failure_var);
                    call_mappings.emplace(call_symbol,
                                          "workflow " + workflow.get().name + " node " + node.name +
                                              " calls " + called_target + source);

                    const auto effect_kind =
                        capability_effect_kind_name(capability->get().effect.kind);
                    const auto effect_symbol =
                        workflow_node_effect_event_var(workflow.get(), node.name, effect_kind);
                    effect_guards[effect_symbol].push_back(guard);
                    event_failure_vars.emplace(effect_symbol, failure_var);
                    effect_mappings.emplace(effect_symbol,
                                            "workflow " + workflow.get().name + " node " +
                                                node.name + " effect " + effect_kind + source);
                }
            }
        }
    }

    for (const auto &[symbol, guards] : call_guards) {
        defines_.push_back(symbol + " := " + render_disjunction(guards) + ";");
        add_symbol_mapping(symbol, call_mappings.at(symbol));
        if (const auto failure_var = event_failure_vars.find(symbol);
            failure_var != event_failure_vars.end()) {
            const auto committed = symbol + "__committed";
            const auto failed = symbol + "__failed";
            defines_.push_back(committed + " := (" + symbol + " & !" + failure_var->second + ");");
            defines_.push_back(failed + " := (" + symbol + " & " + failure_var->second + ");");
            add_symbol_mapping(committed, call_mappings.at(symbol) + " committed");
            add_symbol_mapping(failed, call_mappings.at(symbol) + " failed");
        }
    }

    for (const auto &[symbol, guards] : effect_guards) {
        defines_.push_back(symbol + " := " + render_disjunction(guards) + ";");
        add_symbol_mapping(symbol, effect_mappings.at(symbol));
        if (const auto failure_var = event_failure_vars.find(symbol);
            failure_var != event_failure_vars.end()) {
            const auto committed = symbol + "__committed";
            const auto failed = symbol + "__failed";
            defines_.push_back(committed + " := (" + symbol + " & !" + failure_var->second + ");");
            defines_.push_back(failed + " := (" + symbol + " & " + failure_var->second + ");");
            add_symbol_mapping(committed, effect_mappings.at(symbol) + " committed");
            add_symbol_mapping(failed, effect_mappings.at(symbol) + " failed");
        }
    }
}

void SmvPrinter::collect_called_observation_defines() {
    std::map<std::string, std::string> symbols(called_observation_symbols_.begin(),
                                               called_observation_symbols_.end());

    for (const auto &[key, symbol] : symbols) {
        const auto separator = key.find('\n');
        if (separator == std::string::npos) {
            continue;
        }

        const auto agent_name = key.substr(0, separator);
        const auto capability_name = key.substr(separator + 1);
        defines_.push_back(
            symbol +
            " := " + render_disjunction(collect_called_guards(agent_name, capability_name)) + ";");
    }
}

std::string SmvPrinter::lookup_called_observation_symbol(std::string_view agent_name,
                                                         std::string_view capability_name) const {
    const auto key = called_observation_key(agent_name, capability_name);
    if (const auto iter = called_observation_symbols_.find(key);
        iter != called_observation_symbols_.end()) {
        return iter->second;
    }

    return "agent__" + sanitize_identifier(agent_name) + "__called__" +
           sanitize_identifier(capability_name);
}

std::string SmvPrinter::lookup_embedded_observation_symbol(ir::FormalObservationScopeKind kind,
                                                           std::string_view owner,
                                                           std::size_t clause_index,
                                                           std::size_t atom_index) const {
    const auto key = embedded_observation_key(kind, owner, clause_index, atom_index);
    if (const auto iter = embedded_observation_symbols_.find(key);
        iter != embedded_observation_symbols_.end()) {
        return iter->second;
    }

    const auto base_name =
        kind == ir::FormalObservationScopeKind::ContractClause
            ? "contract__" + std::string(owner) + "__" + std::to_string(clause_index)
            : "workflow__" + std::string(owner) + "__" +
                  std::string(kind == ir::FormalObservationScopeKind::WorkflowSafetyClause
                                  ? "safety"
                                  : "liveness") +
                  "__" + std::to_string(clause_index);
    return clause_atom_name(base_name, atom_index);
}

void SmvPrinter::collect_workflow_node_assignments(const ir::WorkflowDecl &workflow,
                                                   const ir::WorkflowNode &node,
                                                   const ir::AgentDecl &agent) {
    const auto state_var = workflow_node_state_var(workflow, node.name);
    const auto phase_var = workflow_node_phase_var(workflow, node.name);
    const auto failure_requested_var = workflow_node_failure_requested_var(workflow, node.name);
    const auto dependency_guard = render_dependency_guard(workflow, node.after);
    const auto final_guard = render_state_membership(state_var, agent.final_states);

    collect_state_machine_assignments(agent,
                                      state_var,
                                      build_effective_adjacency(agent),
                                      "!(" + phase_var + " = " + std::string(kNodeRunning) + ")");

    assignments_.push_back("init(" + phase_var + ") := " +
                           std::string(node.after.empty() ? kNodeRunning : kNodeIdle) + ";");
    assignments_.push_back("next(" + phase_var + ") := case");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeCompleted) + " : " +
                           std::string(kNodeCompleted) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeTerminalFailed) + " : " +
                           std::string(kNodeTerminalFailed) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeCompensated) + " : " +
                           std::string(kNodeTerminalFailed) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeCompensating) + " : " +
                           std::string(kNodeCompensated) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeRecovered) + " : " +
                           std::string(kNodeRunning) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeRecovering) + " : " +
                           std::string(kNodeRecovered) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeFailed) + " : " +
                           std::string(kNodeRecovering) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeIdle) + " & (" +
                           dependency_guard + ") : " + std::string(kNodeRunning) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeIdle) + " : " +
                           std::string(kNodeIdle) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeRunning) + " & " +
                           final_guard + " : " + std::string(kNodeCompleted) + ";");
    assignments_.push_back("  " + phase_var + " = " + std::string(kNodeRunning) + " & " +
                           failure_requested_var + " : " + std::string(kNodeFailed) + ";");
    assignments_.push_back("  TRUE : " + phase_var + ";");
    assignments_.push_back("esac;");
}

void SmvPrinter::collect_state_machine_assignments(
    const ir::AgentDecl &agent,
    std::string state_var,
    const std::unordered_map<std::string, std::vector<std::string>> &adjacency,
    std::string block_guard) {
    assignments_.push_back("init(" + state_var + ") := " + agent.initial_state + ";");
    assignments_.push_back("next(" + state_var + ") := case");

    if (!block_guard.empty()) {
        assignments_.push_back("  " + block_guard + " : " + state_var + ";");
    }

    for (const auto &state : agent.states) {
        const auto iter = adjacency.find(state);
        const auto next_targets =
            iter == adjacency.end() ? std::vector<std::string>{} : iter->second;
        const auto rendered_targets = render_next_targets(next_targets, state);

        assignments_.push_back("  " + state_var + " = " + state + " : " + rendered_targets + ";");
    }

    assignments_.push_back("  TRUE : " + state_var + ";");
    assignments_.push_back("esac;");
}

std::unordered_map<std::string, std::vector<std::string>>
SmvPrinter::build_effective_adjacency(const ir::AgentDecl &agent) const {
    std::unordered_map<std::string, std::vector<std::string>> adjacency;

    for (const auto &transition : agent.transitions) {
        adjacency[transition.from_state].push_back(transition.to_state);
    }

    const auto flow = find_flow(agent.name);
    if (!flow.has_value()) {
        return adjacency;
    }

    for (const auto &handler : flow->get().state_handlers) {
        const auto *summary = program_ == nullptr
                                  ? nullptr
                                  : ir::find_state_handler_summary(*program_, flow->get(), handler);
        if (summary != nullptr && (!summary->goto_targets.empty() || summary->may_return)) {
            adjacency[handler.state_name] = summary->goto_targets;
        }
    }

    return adjacency;
}

std::string SmvPrinter::render_next_targets(const std::vector<std::string> &targets,
                                            std::string_view fallback_state) const {
    if (targets.empty()) {
        return std::string(fallback_state);
    }

    if (targets.size() == 1) {
        return targets.front();
    }

    return "{" + join(targets, ", ") + "}";
}

std::string SmvPrinter::render_dependency_guard(const ir::WorkflowDecl &workflow,
                                                const std::vector<std::string> &after) const {
    if (after.empty()) {
        return "TRUE";
    }

    std::vector<std::string> guards;
    guards.reserve(after.size());

    for (const auto &dependency : after) {
        guards.push_back(workflow_node_completed_var(workflow, dependency));
    }

    return join(guards, " & ");
}

} // namespace ahfl

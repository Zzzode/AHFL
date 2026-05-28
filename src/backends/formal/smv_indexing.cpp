#include "backends/formal/smv_printer.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

void SmvPrinter::index_declarations(const ir::Program &program) {
    for (const auto &declaration : program.declarations) {
        std::visit(
            Overloaded{
                [&](const ir::AgentDecl &agent) {
                    agents_.emplace(agent.name, std::cref(agent));
                },
                [&](const ir::ContractDecl &contract) {
                    contracts_.push_back(std::cref(contract));
                },
                [&](const ir::CapabilityDecl &capability) {
                    capabilities_.emplace(capability.name, std::cref(capability));
                },
                [&](const ir::FlowDecl &flow) { flows_.emplace(flow.target, std::cref(flow)); },
                [&](const ir::WorkflowDecl &workflow) {
                    workflows_.push_back(std::cref(workflow));
                },
                [&](const auto &) {},
            },
            declaration);
    }
}

void SmvPrinter::index_observations(const ir::Program &program) {
    for (const auto &observation : program.formal_observations) {
        std::visit(
            Overloaded{
                [&](const ir::CalledCapabilityObservation &value) {
                    called_observation_symbols_.emplace(
                        called_observation_key(value.agent, value.capability),
                        observation.symbol);
                    add_symbol_mapping(observation.symbol,
                                       "agent " + value.agent + " called " + value.capability);
                },
                [&](const ir::EmbeddedBoolObservation &value) {
                    if (!embedded_observation_requires_variable(value.scope)) {
                        return;
                    }

                    observation_variables_.push_back(observation.symbol + " : boolean;");
                    embedded_observation_symbols_.emplace(
                        embedded_observation_key(value.scope.kind,
                                                 value.scope.owner,
                                                 value.scope.clause_index,
                                                 value.scope.atom_index),
                        observation.symbol);
                    add_symbol_mapping(
                        observation.symbol,
                        "observation " + smv::observation_scope_kind_key(value.scope.kind) + " " +
                            value.scope.owner + "[" + std::to_string(value.scope.clause_index) +
                            "]" + observation_source_suffix(value.scope));
                },
            },
            observation.node);
    }
}

MaybeCRef<ir::AgentDecl> SmvPrinter::find_agent(std::string_view canonical_name) const {
    if (const auto iter = agents_.find(std::string(canonical_name)); iter != agents_.end()) {
        return std::cref(iter->second.get());
    }

    return std::nullopt;
}

bool SmvPrinter::agent_has_contract(const ir::AgentDecl &agent) const {
    return std::ranges::any_of(
        contracts_, [&](const auto &contract) { return contract.get().target == agent.name; });
}

std::unordered_map<std::string, WorkflowNodeInfo>
SmvPrinter::build_workflow_node_map(const ir::WorkflowDecl &workflow) const {
    std::unordered_map<std::string, WorkflowNodeInfo> nodes;

    for (const auto &node : workflow.nodes) {
        const auto agent = find_agent(node.target);
        if (!agent.has_value()) {
            continue;
        }

        nodes.emplace(node.name,
                      WorkflowNodeInfo{
                          .node = std::cref(node),
                          .agent = std::cref(agent->get()),
                      });
    }

    return nodes;
}

MaybeCRef<ir::FlowDecl> SmvPrinter::find_flow(std::string_view canonical_name) const {
    if (const auto iter = flows_.find(std::string(canonical_name)); iter != flows_.end()) {
        return std::cref(iter->second.get());
    }

    return std::nullopt;
}

MaybeCRef<ir::CapabilityDecl>
SmvPrinter::find_capability(std::string_view canonical_name) const {
    if (const auto iter = capabilities_.find(std::string(canonical_name));
        iter != capabilities_.end()) {
        return std::cref(iter->second.get());
    }

    const auto match = std::ranges::find_if(capabilities_, [&](const auto &entry) {
        return capability_name_matches(entry.first, canonical_name);
    });
    if (match != capabilities_.end()) {
        return std::cref(match->second.get());
    }

    return std::nullopt;
}

void SmvPrinter::add_symbol_mapping(const std::string &symbol, const std::string &description) {
    const auto mapping = symbol + " => " + description;
    if (std::ranges::find(symbol_mappings_, mapping) == symbol_mappings_.end()) {
        symbol_mappings_.push_back(mapping);
    }
}

std::string SmvPrinter::with_source(std::string description,
                                    std::string_view source_path,
                                    const ir::SourceRangeOpt &range) const {
    description += source_suffix(source_path, range);
    return description;
}

std::string SmvPrinter::contract_clause_source_suffix(std::string_view owner,
                                                      std::size_t clause_index) const {
    for (const auto &contract : contracts_) {
        if (contract.get().target != owner || clause_index >= contract.get().clauses.size()) {
            continue;
        }

        const auto &clause = contract.get().clauses[clause_index];
        return source_suffix(contract.get().provenance.source_path, clause.source_range);
    }

    return {};
}

std::string SmvPrinter::workflow_clause_source_suffix(std::string_view owner,
                                                      ir::FormalObservationScopeKind kind,
                                                      std::size_t clause_index) const {
    for (const auto &workflow : workflows_) {
        if (workflow.get().name != owner) {
            continue;
        }

        if (kind == ir::FormalObservationScopeKind::WorkflowSafetyClause &&
            clause_index < workflow.get().safety.size()) {
            return source_suffix(workflow.get().provenance.source_path,
                                 workflow.get().safety[clause_index]->source_range);
        }

        if (kind == ir::FormalObservationScopeKind::WorkflowLivenessClause &&
            clause_index < workflow.get().liveness.size()) {
            return source_suffix(workflow.get().provenance.source_path,
                                 workflow.get().liveness[clause_index]->source_range);
        }
    }

    return {};
}

std::string
SmvPrinter::observation_source_suffix(const ir::FormalObservationScope &scope) const {
    if (scope.kind == ir::FormalObservationScopeKind::ContractClause) {
        return contract_clause_source_suffix(scope.owner, scope.clause_index);
    }

    return workflow_clause_source_suffix(scope.owner, scope.kind, scope.clause_index);
}

const ir::Expr *SmvPrinter::find_embedded_expr_by_atom(const ir::TemporalExpr &expr,
                                                        std::size_t target_atom,
                                                        std::size_t &current_atom) const {
    return std::visit(
        Overloaded{
            [&](const ir::EmbeddedTemporalExpr &value) -> const ir::Expr * {
                if (current_atom == target_atom) {
                    return value.expr.get();
                }
                ++current_atom;
                return nullptr;
            },
            [&](const ir::TemporalUnaryExpr &value) -> const ir::Expr * {
                return find_embedded_expr_by_atom(*value.operand, target_atom, current_atom);
            },
            [&](const ir::TemporalBinaryExpr &value) -> const ir::Expr * {
                if (const auto *lhs =
                        find_embedded_expr_by_atom(*value.lhs, target_atom, current_atom);
                    lhs != nullptr) {
                    return lhs;
                }
                return find_embedded_expr_by_atom(*value.rhs, target_atom, current_atom);
            },
            [](const auto &) -> const ir::Expr * { return nullptr; },
        },
        expr.node);
}

const ir::Expr *
SmvPrinter::find_embedded_observation_expr(const ir::FormalObservationScope &scope) const {
    if (scope.kind == ir::FormalObservationScopeKind::ContractClause) {
        for (const auto &contract : contracts_) {
            if (contract.get().target != scope.owner ||
                scope.clause_index >= contract.get().clauses.size()) {
                continue;
            }

            const auto &clause = contract.get().clauses[scope.clause_index];
            if (const auto *expr = std::get_if<ir::ExprPtr>(&clause.value);
                expr != nullptr && scope.atom_index == 0) {
                return expr->get();
            }

            if (const auto *temporal = std::get_if<ir::TemporalExprPtr>(&clause.value);
                temporal != nullptr) {
                std::size_t current_atom = 0;
                return find_embedded_expr_by_atom(**temporal, scope.atom_index, current_atom);
            }
        }
        return nullptr;
    }

    for (const auto &workflow : workflows_) {
        if (workflow.get().name != scope.owner) {
            continue;
        }

        if (scope.kind == ir::FormalObservationScopeKind::WorkflowSafetyClause &&
            scope.clause_index < workflow.get().safety.size()) {
            std::size_t current_atom = 0;
            return find_embedded_expr_by_atom(
                *workflow.get().safety[scope.clause_index], scope.atom_index, current_atom);
        }

        if (scope.kind == ir::FormalObservationScopeKind::WorkflowLivenessClause &&
            scope.clause_index < workflow.get().liveness.size()) {
            std::size_t current_atom = 0;
            return find_embedded_expr_by_atom(
                *workflow.get().liveness[scope.clause_index], scope.atom_index, current_atom);
        }
    }

    return nullptr;
}

bool
SmvPrinter::embedded_observation_requires_variable(const ir::FormalObservationScope &scope) const {
    const auto *expr = find_embedded_observation_expr(scope);
    return expr == nullptr || !render_bounded_expr(*expr).has_value();
}

bool SmvPrinter::handler_calls_capability(const ir::StateHandler &handler,
                                           std::string_view capability_name) const {
    return std::ranges::any_of(handler.summary.called_targets, [&](const auto &target) {
        return capability_name_matches(target, capability_name);
    });
}

std::string
SmvPrinter::render_workflow_node_state_guard(const ir::WorkflowDecl &workflow,
                                             const ir::WorkflowNode &node,
                                             const ir::StateHandler &handler) const {
    return "(" + workflow_node_running_var(workflow, node.name) + " & (" +
           workflow_node_state_var(workflow, node.name) + " = " + handler.state_name + "))";
}

std::vector<std::string>
SmvPrinter::collect_called_guards(std::string_view agent_name,
                                  std::string_view capability_name) const {
    std::vector<std::string> guards;

    for (const auto &workflow : workflows_) {
        for (const auto &node : workflow.get().nodes) {
            if (node.target != agent_name) {
                continue;
            }

            const auto flow = find_flow(node.target);
            if (!flow.has_value()) {
                continue;
            }

            for (const auto &handler : flow->get().state_handlers) {
                if (handler_calls_capability(handler, capability_name)) {
                    guards.push_back(
                        render_workflow_node_state_guard(workflow.get(), node, handler));
                }
            }
        }
    }

    if (!guards.empty()) {
        return guards;
    }

    const auto agent = find_agent(agent_name);
    const auto flow = find_flow(agent_name);
    if (!agent.has_value() || !flow.has_value() || !agent_has_contract(agent->get())) {
        return guards;
    }

    for (const auto &handler : flow->get().state_handlers) {
        if (handler_calls_capability(handler, capability_name)) {
            guards.push_back("(" + agent_state_var(agent->get()) + " = " + handler.state_name +
                             ")");
        }
    }

    return guards;
}

} // namespace ahfl

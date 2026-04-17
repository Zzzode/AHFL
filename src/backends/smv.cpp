#include "ahfl/backends/smv.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct WorkflowNodeInfo {
    std::reference_wrapper<const ir::WorkflowNode> node;
    std::reference_wrapper<const ir::AgentDecl> agent;
};

[[nodiscard]] std::string join(const std::vector<std::string> &parts, std::string_view delimiter) {
    std::string joined;

    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            joined += delimiter;
        }
        joined += parts[index];
    }

    return joined;
}

[[nodiscard]] std::string sanitize_identifier(std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size() + 2);

    for (const auto character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            sanitized.push_back(character);
        } else {
            sanitized.push_back('_');
        }
    }

    while (sanitized.find("__") != std::string::npos) {
        sanitized.replace(sanitized.find("__"), 2, "_");
    }

    if (sanitized.empty()) {
        return "id";
    }

    if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0) {
        sanitized.insert(sanitized.begin(), 'n');
        sanitized.insert(sanitized.begin() + 1, '_');
    }

    return sanitized;
}

[[nodiscard]] std::string smv_unary_op(ir::TemporalUnaryOp op) {
    switch (op) {
    case ir::TemporalUnaryOp::Always:
        return "G";
    case ir::TemporalUnaryOp::Eventually:
        return "F";
    case ir::TemporalUnaryOp::Next:
        return "X";
    case ir::TemporalUnaryOp::Not:
        return "!";
    }

    return "!";
}

[[nodiscard]] std::string smv_binary_op(ir::TemporalBinaryOp op) {
    switch (op) {
    case ir::TemporalBinaryOp::Implies:
        return "->";
    case ir::TemporalBinaryOp::Or:
        return "|";
    case ir::TemporalBinaryOp::And:
        return "&";
    case ir::TemporalBinaryOp::Until:
        return "U";
    }

    return "&";
}

[[nodiscard]] std::string agent_state_var(const ir::AgentDecl &agent) {
    return "agent__" + sanitize_identifier(agent.name) + "__state";
}

[[nodiscard]] std::string workflow_node_state_var(const ir::WorkflowDecl &workflow,
                                                  std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__state";
}

[[nodiscard]] std::string workflow_node_completed_var(const ir::WorkflowDecl &workflow,
                                                      std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__completed";
}

[[nodiscard]] std::string workflow_node_started_var(const ir::WorkflowDecl &workflow,
                                                    std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__started";
}

[[nodiscard]] std::string workflow_node_running_var(const ir::WorkflowDecl &workflow,
                                                    std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__running";
}

[[nodiscard]] std::string clause_atom_name(std::string_view base_name, std::size_t index) {
    return sanitize_identifier(std::string(base_name) + "__atom__" + std::to_string(index));
}

[[nodiscard]] std::string observation_scope_kind_key(ir::FormalObservationScopeKind kind) {
    switch (kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract";
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow_safety";
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow_liveness";
    }

    return "invalid";
}

[[nodiscard]] std::string called_observation_key(std::string_view agent_name,
                                                 std::string_view capability_name) {
    return std::string(agent_name) + "\n" + std::string(capability_name);
}

[[nodiscard]] std::string embedded_observation_key(ir::FormalObservationScopeKind kind,
                                                   std::string_view owner,
                                                   std::size_t clause_index,
                                                   std::size_t atom_index) {
    return observation_scope_kind_key(kind) + "\n" + std::string(owner) + "\n" +
           std::to_string(clause_index) + "\n" + std::to_string(atom_index);
}

[[nodiscard]] std::string render_state_membership(std::string_view state_var,
                                                  const std::vector<std::string> &states) {
    if (states.empty()) {
        return "FALSE";
    }

    std::vector<std::string> clauses;
    clauses.reserve(states.size());

    for (const auto &state : states) {
        clauses.push_back("(" + std::string(state_var) + " = " + state + ")");
    }

    if (clauses.size() == 1) {
        return clauses.front();
    }

    return "(" + join(clauses, " | ") + ")";
}

[[nodiscard]] std::string contract_clause_kind_name(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return "requires";
    case ir::ContractClauseKind::Ensures:
        return "ensures";
    case ir::ContractClauseKind::Invariant:
        return "invariant";
    case ir::ContractClauseKind::Forbid:
        return "forbid";
    }

    return "invalid";
}

class SmvPrinter final {
  public:
    explicit SmvPrinter(std::ostream &out) : out_(out) {}

    void print(const ir::Program &program) {
        index_declarations(program);
        index_observations(program);
        collect_state_variables();
        collect_defines();
        collect_assignments();
        collect_specs();
        emit();
    }

  private:
    std::ostream &out_;
    std::unordered_map<std::string, std::reference_wrapper<const ir::AgentDecl>> agents_;
    std::unordered_map<std::string, std::reference_wrapper<const ir::FlowDecl>> flows_;
    std::vector<std::reference_wrapper<const ir::ContractDecl>> contracts_;
    std::vector<std::reference_wrapper<const ir::WorkflowDecl>> workflows_;
    std::vector<std::string> state_variables_;
    std::vector<std::string> observation_variables_;
    std::unordered_map<std::string, std::string> called_observation_symbols_;
    std::unordered_map<std::string, std::string> embedded_observation_symbols_;
    std::vector<std::string> defines_;
    std::vector<std::string> assignments_;
    std::vector<std::string> specs_;

    void index_declarations(const ir::Program &program) {
        for (const auto &declaration : program.declarations) {
            std::visit(
                Overloaded{
                    [&](const ir::AgentDecl &agent) {
                        agents_.emplace(agent.name, std::cref(agent));
                    },
                    [&](const ir::ContractDecl &contract) {
                        contracts_.push_back(std::cref(contract));
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

    void index_observations(const ir::Program &program) {
        for (const auto &observation : program.formal_observations) {
            observation_variables_.push_back(observation.symbol + " : boolean;");

            std::visit(Overloaded{
                           [&](const ir::CalledCapabilityObservation &value) {
                               called_observation_symbols_.emplace(
                                   called_observation_key(value.agent, value.capability),
                                   observation.symbol);
                           },
                           [&](const ir::EmbeddedBoolObservation &value) {
                               embedded_observation_symbols_.emplace(
                                   embedded_observation_key(value.scope.kind,
                                                            value.scope.owner,
                                                            value.scope.clause_index,
                                                            value.scope.atom_index),
                                   observation.symbol);
                           },
                       },
                       observation.node);
        }
    }

    void collect_state_variables() {
        for (const auto &[name, agent] : agents_) {
            (void)name;
            state_variables_.push_back(agent_state_var(agent.get()) + " : {" +
                                       join(agent.get().states, ", ") + "};");
        }

        for (const auto &workflow : workflows_) {
            for (const auto &node : workflow.get().nodes) {
                const auto agent = find_agent(node.target);
                if (!agent.has_value()) {
                    continue;
                }

                state_variables_.push_back(workflow_node_state_var(workflow.get(), node.name) +
                                           " : {" + join(agent->get().states, ", ") + "};");
                state_variables_.push_back(workflow_node_started_var(workflow.get(), node.name) +
                                           " : boolean;");
                state_variables_.push_back(workflow_node_completed_var(workflow.get(), node.name) +
                                           " : boolean;");
            }
        }
    }

    void collect_defines() {
        for (const auto &workflow : workflows_) {
            const auto node_map = build_workflow_node_map(workflow.get());
            for (const auto &node : workflow.get().nodes) {
                const auto started_name = workflow_node_started_var(workflow.get(), node.name);
                const auto running_name = workflow_node_running_var(workflow.get(), node.name);
                const auto target = node_map.find(node.name);
                if (target == node_map.end()) {
                    continue;
                }

                defines_.push_back(running_name + " := " + started_name + " & !(" +
                                   workflow_node_completed_var(workflow.get(), node.name) + ");");
            }
        }
    }

    void collect_assignments() {
        for (const auto &[name, agent] : agents_) {
            (void)name;
            collect_state_machine_assignments(
                agent.get(), agent_state_var(agent.get()), build_effective_adjacency(agent.get()));
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

    void collect_specs() {
        for (const auto &contract : contracts_) {
            const auto agent = find_agent(contract.get().target);
            if (!agent.has_value()) {
                continue;
            }

            for (std::size_t index = 0; index < contract.get().clauses.size(); ++index) {
                const auto &clause = contract.get().clauses[index];
                std::string formula;
                if (const auto expr = std::get_if<ir::ExprPtr>(&clause.value); expr != nullptr) {
                    formula = render_contract_expr_clause(
                        contract.get(), agent->get(), clause.kind, index);
                } else if (const auto temporal = std::get_if<ir::TemporalExprPtr>(&clause.value);
                           temporal != nullptr) {
                    std::size_t atom_index = 0;
                    formula = render_agent_formula(**temporal, agent->get(), index, atom_index);
                } else {
                    continue;
                }

                specs_.push_back("-- contract " + contract.get().target + " " +
                                 contract_clause_kind_name(clause.kind) + "[" +
                                 std::to_string(index) + "]");
                specs_.push_back("LTLSPEC " + formula);
            }
        }

        for (const auto &workflow : workflows_) {
            for (std::size_t index = 0; index < workflow.get().safety.size(); ++index) {
                std::size_t atom_index = 0;
                const auto formula =
                    render_workflow_formula(*workflow.get().safety[index],
                                            workflow.get(),
                                            ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                            index,
                                            atom_index);
                specs_.push_back("-- workflow " + workflow.get().name + " safety[" +
                                 std::to_string(index) + "]");
                specs_.push_back("LTLSPEC " + formula);
            }

            for (std::size_t index = 0; index < workflow.get().liveness.size(); ++index) {
                std::size_t atom_index = 0;
                const auto formula =
                    render_workflow_formula(*workflow.get().liveness[index],
                                            workflow.get(),
                                            ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                            index,
                                            atom_index);
                specs_.push_back("-- workflow " + workflow.get().name + " liveness[" +
                                 std::to_string(index) + "]");
                specs_.push_back("LTLSPEC " + formula);
            }
        }
    }

    [[nodiscard]] MaybeCRef<ir::AgentDecl> find_agent(std::string_view canonical_name) const {
        if (const auto iter = agents_.find(std::string(canonical_name)); iter != agents_.end()) {
            return std::cref(iter->second.get());
        }

        return std::nullopt;
    }

    [[nodiscard]] std::unordered_map<std::string, WorkflowNodeInfo>
    build_workflow_node_map(const ir::WorkflowDecl &workflow) const {
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

    [[nodiscard]] MaybeCRef<ir::FlowDecl> find_flow(std::string_view canonical_name) const {
        if (const auto iter = flows_.find(std::string(canonical_name)); iter != flows_.end()) {
            return std::cref(iter->second.get());
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string
    lookup_called_observation_symbol(std::string_view agent_name,
                                     std::string_view capability_name) const {
        const auto key = called_observation_key(agent_name, capability_name);
        if (const auto iter = called_observation_symbols_.find(key);
            iter != called_observation_symbols_.end()) {
            return iter->second;
        }

        return "agent__" + sanitize_identifier(agent_name) + "__called__" +
               sanitize_identifier(capability_name);
    }

    [[nodiscard]] std::string
    lookup_embedded_observation_symbol(ir::FormalObservationScopeKind kind,
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

    void collect_workflow_node_assignments(const ir::WorkflowDecl &workflow,
                                           const ir::WorkflowNode &node,
                                           const ir::AgentDecl &agent) {
        const auto state_var = workflow_node_state_var(workflow, node.name);
        const auto started_var = workflow_node_started_var(workflow, node.name);
        const auto completed_var = workflow_node_completed_var(workflow, node.name);
        const auto dependency_guard = render_dependency_guard(workflow, node.after);

        collect_state_machine_assignments(agent,
                                          state_var,
                                          build_effective_adjacency(agent),
                                          node.after.empty() ? std::string()
                                                             : "!(" + started_var + ") & !(" +
                                                                   dependency_guard + ")");

        assignments_.push_back("init(" + started_var +
                               ") := " + std::string(node.after.empty() ? "TRUE" : "FALSE") + ";");
        if (node.after.empty()) {
            assignments_.push_back("next(" + started_var + ") := TRUE;");
        } else {
            assignments_.push_back("next(" + started_var + ") := case");
            assignments_.push_back("  " + started_var + " : TRUE;");
            assignments_.push_back("  " + dependency_guard + " : TRUE;");
            assignments_.push_back("  TRUE : FALSE;");
            assignments_.push_back("esac;");
        }

        assignments_.push_back("init(" + completed_var + ") := FALSE;");
        assignments_.push_back("next(" + completed_var + ") := case");
        assignments_.push_back("  " + completed_var + " : TRUE;");
        assignments_.push_back("  " + started_var + " & " +
                               render_state_membership(state_var, agent.final_states) + " : TRUE;");
        assignments_.push_back("  TRUE : FALSE;");
        assignments_.push_back("esac;");
    }

    void collect_state_machine_assignments(
        const ir::AgentDecl &agent,
        std::string state_var,
        const std::unordered_map<std::string, std::vector<std::string>> &adjacency,
        std::string block_guard = {}) {
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

            assignments_.push_back("  " + state_var + " = " + state + " : " + rendered_targets +
                                   ";");
        }

        assignments_.push_back("  TRUE : " + state_var + ";");
        assignments_.push_back("esac;");
    }

    [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
    build_effective_adjacency(const ir::AgentDecl &agent) const {
        std::unordered_map<std::string, std::vector<std::string>> adjacency;

        for (const auto &transition : agent.transitions) {
            adjacency[transition.from_state].push_back(transition.to_state);
        }

        const auto flow = find_flow(agent.name);
        if (!flow.has_value()) {
            return adjacency;
        }

        for (const auto &handler : flow->get().state_handlers) {
            if (!handler.summary.goto_targets.empty() || handler.summary.may_return) {
                adjacency[handler.state_name] = handler.summary.goto_targets;
            }
        }

        return adjacency;
    }

    [[nodiscard]] std::string render_next_targets(const std::vector<std::string> &targets,
                                                  std::string_view fallback_state) const {
        if (targets.empty()) {
            return std::string(fallback_state);
        }

        if (targets.size() == 1) {
            return targets.front();
        }

        return "{" + join(targets, ", ") + "}";
    }

    [[nodiscard]] std::string render_dependency_guard(const ir::WorkflowDecl &workflow,
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

    [[nodiscard]] std::string render_agent_formula(const ir::TemporalExpr &expr,
                                                   const ir::AgentDecl &agent,
                                                   std::size_t clause_index,
                                                   std::size_t &atom_index) const {
        return std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    return lookup_embedded_observation_symbol(
                        ir::FormalObservationScopeKind::ContractClause,
                        agent.name,
                        clause_index,
                        atom_index++);
                },
                [&](const ir::CalledTemporalExpr &value) {
                    return lookup_called_observation_symbol(agent.name, value.capability);
                },
                [&](const ir::InStateTemporalExpr &value) {
                    return "(" + agent_state_var(agent) + " = " + value.state + ")";
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    return smv_unary_op(value.op) + " (" +
                           render_agent_formula(*value.operand, agent, clause_index, atom_index) +
                           ")";
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    return "(" + render_agent_formula(*value.lhs, agent, clause_index, atom_index) +
                           " " + smv_binary_op(value.op) + " " +
                           render_agent_formula(*value.rhs, agent, clause_index, atom_index) + ")";
                },
                [&](const auto &) { return std::string("FALSE"); },
            },
            expr.node);
    }

    [[nodiscard]] std::string render_contract_expr_clause(const ir::ContractDecl &contract,
                                                          const ir::AgentDecl &agent,
                                                          ir::ContractClauseKind kind,
                                                          std::size_t clause_index) const {
        const auto observation = lookup_embedded_observation_symbol(
            ir::FormalObservationScopeKind::ContractClause, contract.target, clause_index, 0);

        switch (kind) {
        case ir::ContractClauseKind::Requires:
            // A `requires` clause is exported as an initial-state precondition only.
            return observation;
        case ir::ContractClauseKind::Ensures:
            // An `ensures` clause is only checked once the agent reaches a final state.
            return "G (" + render_state_membership(agent_state_var(agent), agent.final_states) +
                   " -> " + observation + ")";
        case ir::ContractClauseKind::Invariant:
        case ir::ContractClauseKind::Forbid:
            return observation;
        }

        return observation;
    }

    [[nodiscard]] std::string render_workflow_formula(const ir::TemporalExpr &expr,
                                                      const ir::WorkflowDecl &workflow,
                                                      ir::FormalObservationScopeKind scope_kind,
                                                      std::size_t clause_index,
                                                      std::size_t &atom_index) const {
        return std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    return lookup_embedded_observation_symbol(
                        scope_kind, workflow.name, clause_index, atom_index++);
                },
                [&](const ir::RunningTemporalExpr &value) {
                    return workflow_node_running_var(workflow, value.node);
                },
                [&](const ir::CompletedTemporalExpr &value) {
                    if (!value.state_name.has_value()) {
                        return workflow_node_completed_var(workflow, value.node);
                    }

                    return "(" + workflow_node_completed_var(workflow, value.node) + " & (" +
                           workflow_node_state_var(workflow, value.node) + " = " +
                           *value.state_name + "))";
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    return smv_unary_op(value.op) + " (" +
                           render_workflow_formula(
                               *value.operand, workflow, scope_kind, clause_index, atom_index) +
                           ")";
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    return "(" +
                           render_workflow_formula(
                               *value.lhs, workflow, scope_kind, clause_index, atom_index) +
                           " " + smv_binary_op(value.op) + " " +
                           render_workflow_formula(
                               *value.rhs, workflow, scope_kind, clause_index, atom_index) +
                           ")";
                },
                [&](const auto &) { return std::string("FALSE"); },
            },
            expr.node);
    }

    void emit() {
        out_ << "-- AHFL restricted SMV backend v0.1\n";
        out_ << "-- This lowering preserves validated state-machine, flow, and workflow "
                "structure.\n";
        out_ << "-- Pure embedded expressions are abstracted as boolean observation variables.\n";
        out_ << "MODULE main\n";

        if (!state_variables_.empty()) {
            out_ << "VAR\n";
            for (const auto &state_var : state_variables_) {
                out_ << "  " << state_var << '\n';
            }
        }

        if (!observation_variables_.empty()) {
            out_ << "IVAR\n";
            for (const auto &observation : observation_variables_) {
                out_ << "  " << observation << '\n';
            }
        }

        if (!defines_.empty()) {
            out_ << "DEFINE\n";
            for (const auto &define : defines_) {
                out_ << "  " << define << '\n';
            }
        }

        if (!assignments_.empty()) {
            out_ << "ASSIGN\n";
            for (const auto &assignment : assignments_) {
                out_ << "  " << assignment << '\n';
            }
        }

        for (const auto &spec : specs_) {
            out_ << spec << '\n';
        }
    }
};

} // namespace

void print_program_smv(const ir::Program &program, std::ostream &out) {
    SmvPrinter printer(out);
    printer.print(program);
}

void emit_program_smv(const ast::Program &program,
                      const ResolveResult &resolve_result,
                      const TypeCheckResult &type_check_result,
                      std::ostream &out) {
    print_program_smv(lower_program_ir(program, resolve_result, type_check_result), out);
}

} // namespace ahfl

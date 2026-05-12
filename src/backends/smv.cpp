#include "ahfl/backends/smv.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
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

[[nodiscard]] std::string workflow_node_phase_var(const ir::WorkflowDecl &workflow,
                                                  std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__phase";
}

[[nodiscard]] std::string workflow_node_completed_var(const ir::WorkflowDecl &workflow,
                                                      std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__completed";
}

constexpr std::string_view kNodeIdle = "AHFL_NODE_IDLE";
constexpr std::string_view kNodeRunning = "AHFL_NODE_RUNNING";
constexpr std::string_view kNodeCompleted = "AHFL_NODE_COMPLETED";
constexpr std::string_view kNodeFailed = "AHFL_NODE_FAILED";
constexpr std::string_view kNodeRecovering = "AHFL_NODE_RECOVERING";
constexpr std::string_view kNodeRecovered = "AHFL_NODE_RECOVERED";
constexpr std::string_view kNodeCompensating = "AHFL_NODE_COMPENSATING";
constexpr std::string_view kNodeCompensated = "AHFL_NODE_COMPENSATED";
constexpr std::string_view kNodeTerminalFailed = "AHFL_NODE_TERMINAL_FAILED";

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

[[nodiscard]] std::string workflow_node_failure_requested_var(const ir::WorkflowDecl &workflow,
                                                              std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__failure_requested";
}

[[nodiscard]] std::string workflow_node_failed_var(const ir::WorkflowDecl &workflow,
                                                   std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__failed";
}

[[nodiscard]] std::string workflow_node_recovering_var(const ir::WorkflowDecl &workflow,
                                                       std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__recovering";
}

[[nodiscard]] std::string workflow_node_recovered_var(const ir::WorkflowDecl &workflow,
                                                      std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__recovered";
}

[[nodiscard]] std::string workflow_node_call_event_var(const ir::WorkflowDecl &workflow,
                                                       std::string_view node_name,
                                                       std::string_view capability_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__call__" + sanitize_identifier(capability_name);
}

[[nodiscard]] std::string workflow_node_effect_event_var(const ir::WorkflowDecl &workflow,
                                                         std::string_view node_name,
                                                         std::string_view effect_kind) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__effect__" + sanitize_identifier(effect_kind);
}

[[nodiscard]] std::string workflow_node_call_committed_event_var(const ir::WorkflowDecl &workflow,
                                                                 std::string_view node_name,
                                                                 std::string_view capability_name) {
    return workflow_node_call_event_var(workflow, node_name, capability_name) + "__committed";
}

[[nodiscard]] std::string workflow_node_call_failed_event_var(const ir::WorkflowDecl &workflow,
                                                              std::string_view node_name,
                                                              std::string_view capability_name) {
    return workflow_node_call_event_var(workflow, node_name, capability_name) + "__failed";
}

[[nodiscard]] std::string workflow_node_effect_committed_event_var(const ir::WorkflowDecl &workflow,
                                                                   std::string_view node_name,
                                                                   std::string_view effect_kind) {
    return workflow_node_effect_event_var(workflow, node_name, effect_kind) + "__committed";
}

[[nodiscard]] std::string workflow_node_effect_failed_event_var(const ir::WorkflowDecl &workflow,
                                                                std::string_view node_name,
                                                                std::string_view effect_kind) {
    return workflow_node_effect_event_var(workflow, node_name, effect_kind) + "__failed";
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

[[nodiscard]] std::string capability_effect_kind_name(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Read:
        return "read";
    case ir::CapabilityEffectKind::ExternalSideEffect:
        return "external_side_effect";
    case ir::CapabilityEffectKind::DurableWrite:
        return "durable_write";
    case ir::CapabilityEffectKind::FinancialWrite:
        return "financial_write";
    case ir::CapabilityEffectKind::Unknown:
        return "unknown";
    }

    return "unknown";
}

[[nodiscard]] bool capability_name_matches(std::string_view candidate,
                                           std::string_view requested_name) {
    if (candidate == requested_name) {
        return true;
    }

    const auto requested = std::string(requested_name);
    const auto candidate_text = std::string(candidate);
    if (candidate_text.size() <= requested.size()) {
        return false;
    }

    return candidate_text.ends_with("::" + requested);
}

[[nodiscard]] bool has_policy(const ir::CapabilityEffectSpec &effect,
                              std::string_view requested_policy) {
    return std::ranges::any_of(effect.policies, [&](const auto &policy) {
        return policy == requested_policy || capability_name_matches(policy, requested_policy);
    });
}

[[nodiscard]] bool is_external_or_stronger(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::ExternalSideEffect ||
           kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
}

[[nodiscard]] bool is_durable_or_stronger(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
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

[[nodiscard]] std::string render_disjunction(const std::vector<std::string> &guards) {
    if (guards.empty()) {
        return "FALSE";
    }

    if (guards.size() == 1) {
        return guards.front();
    }

    return "(" + join(guards, " | ") + ")";
}

[[nodiscard]] std::string render_conjunction(const std::vector<std::string> &guards) {
    if (guards.empty()) {
        return "TRUE";
    }

    if (guards.size() == 1) {
        return guards.front();
    }

    return "(" + join(guards, " & ") + ")";
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

[[nodiscard]] bool is_integer_literal_spelling(std::string_view spelling) {
    if (spelling.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (spelling.front() == '-' || spelling.front() == '+') {
        index = 1;
    }

    if (index == spelling.size()) {
        return false;
    }

    for (; index < spelling.size(); ++index) {
        if (std::isdigit(static_cast<unsigned char>(spelling[index])) == 0) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::optional<std::string> smv_expr_unary_op(ir::ExprUnaryOp op) {
    switch (op) {
    case ir::ExprUnaryOp::Not:
        return "!";
    case ir::ExprUnaryOp::Negate:
        return "-";
    case ir::ExprUnaryOp::Positive:
        return "";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> smv_expr_binary_op(ir::ExprBinaryOp op) {
    switch (op) {
    case ir::ExprBinaryOp::Implies:
        return "->";
    case ir::ExprBinaryOp::Or:
        return "|";
    case ir::ExprBinaryOp::And:
        return "&";
    case ir::ExprBinaryOp::Equal:
        return "=";
    case ir::ExprBinaryOp::NotEqual:
        return "!=";
    case ir::ExprBinaryOp::Less:
        return "<";
    case ir::ExprBinaryOp::LessEqual:
        return "<=";
    case ir::ExprBinaryOp::Greater:
        return ">";
    case ir::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ir::ExprBinaryOp::Add:
        return "+";
    case ir::ExprBinaryOp::Subtract:
        return "-";
    case ir::ExprBinaryOp::Multiply:
        return "*";
    case ir::ExprBinaryOp::Divide:
        return "/";
    case ir::ExprBinaryOp::Modulo:
        return "mod";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> render_bounded_expr(const ir::Expr &expr) {
    return std::visit(Overloaded{
                          [](const ir::BoolLiteralExpr &value) -> std::optional<std::string> {
                              return value.value ? "TRUE" : "FALSE";
                          },
                          [](const ir::IntegerLiteralExpr &value) -> std::optional<std::string> {
                              if (!is_integer_literal_spelling(value.spelling)) {
                                  return std::nullopt;
                              }
                              return value.spelling;
                          },
                          [](const ir::UnaryExpr &value) -> std::optional<std::string> {
                              const auto operand = render_bounded_expr(*value.operand);
                              const auto op = smv_expr_unary_op(value.op);
                              if (!operand.has_value() || !op.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *op + "(" + *operand + "))";
                          },
                          [](const ir::BinaryExpr &value) -> std::optional<std::string> {
                              const auto lhs = render_bounded_expr(*value.lhs);
                              const auto rhs = render_bounded_expr(*value.rhs);
                              const auto op = smv_expr_binary_op(value.op);
                              if (!lhs.has_value() || !rhs.has_value() || !op.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *lhs + " " + *op + " " + *rhs + ")";
                          },
                          [](const ir::GroupExpr &value) -> std::optional<std::string> {
                              const auto nested = render_bounded_expr(*value.expr);
                              if (!nested.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *nested + ")";
                          },
                          [](const auto &) -> std::optional<std::string> { return std::nullopt; },
                      },
                      expr.node);
}

[[nodiscard]] std::string source_suffix(std::string_view source_path,
                                        const ir::SourceRangeOpt &range) {
    if (source_path.empty() && !range.has_value()) {
        return {};
    }

    std::string suffix = " @ ";
    suffix += source_path.empty() ? "<unknown>" : std::string(source_path);
    if (range.has_value()) {
        suffix += ":";
        suffix += std::to_string(range->begin_offset);
        suffix += "-";
        suffix += std::to_string(range->end_offset);
    }
    return suffix;
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
    std::unordered_map<std::string, std::reference_wrapper<const ir::CapabilityDecl>> capabilities_;
    std::vector<std::reference_wrapper<const ir::ContractDecl>> contracts_;
    std::vector<std::reference_wrapper<const ir::WorkflowDecl>> workflows_;
    std::vector<std::string> state_variables_;
    std::vector<std::string> observation_variables_;
    std::unordered_map<std::string, std::string> called_observation_symbols_;
    std::unordered_map<std::string, std::string> embedded_observation_symbols_;
    std::vector<std::string> symbol_mappings_;
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

    void index_observations(const ir::Program &program) {
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
                            "observation " + observation_scope_kind_key(value.scope.kind) + " " +
                                value.scope.owner + "[" + std::to_string(value.scope.clause_index) +
                                "]" + observation_source_suffix(value.scope));
                    },
                },
                observation.node);
        }
    }

    void collect_state_variables() {
        for (const auto &[name, agent] : agents_) {
            (void)name;
            if (agent_has_contract(agent.get())) {
                const auto state_var = agent_state_var(agent.get());
                state_variables_.push_back(state_var + " : {" + join(agent.get().states, ", ") +
                                           "};");
                add_symbol_mapping(state_var,
                                   with_source("agent " + agent.get().name + " state",
                                               agent.get().provenance.source_path,
                                               agent.get().provenance.source_range));
            }
        }

        for (const auto &workflow : workflows_) {
            for (const auto &node : workflow.get().nodes) {
                const auto agent = find_agent(node.target);
                if (!agent.has_value()) {
                    continue;
                }

                const auto state_var = workflow_node_state_var(workflow.get(), node.name);
                const auto phase_var = workflow_node_phase_var(workflow.get(), node.name);
                const auto failure_requested_var =
                    workflow_node_failure_requested_var(workflow.get(), node.name);
                state_variables_.push_back(state_var + " : {" + join(agent->get().states, ", ") +
                                           "};");
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

    void collect_defines() {
        for (const auto &workflow : workflows_) {
            const auto node_map = build_workflow_node_map(workflow.get());
            for (const auto &node : workflow.get().nodes) {
                const auto started_name = workflow_node_started_var(workflow.get(), node.name);
                const auto running_name = workflow_node_running_var(workflow.get(), node.name);
                const auto completed_name = workflow_node_completed_var(workflow.get(), node.name);
                const auto failed_name = workflow_node_failed_var(workflow.get(), node.name);
                const auto recovering_name =
                    workflow_node_recovering_var(workflow.get(), node.name);
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
                add_symbol_mapping(completed_name,
                                   with_source("workflow " + workflow.get().name + " node " +
                                                   node.name + " completed",
                                               workflow.get().provenance.source_path,
                                               node.source_range));
                add_symbol_mapping(failed_name,
                                   with_source("workflow " + workflow.get().name + " node " +
                                                   node.name + " failed",
                                               workflow.get().provenance.source_path,
                                               node.source_range));
                add_symbol_mapping(recovering_name,
                                   with_source("workflow " + workflow.get().name + " node " +
                                                   node.name + " recovering",
                                               workflow.get().provenance.source_path,
                                               node.source_range));
                add_symbol_mapping(recovered_name,
                                   with_source("workflow " + workflow.get().name + " node " +
                                                   node.name + " recovered",
                                               workflow.get().provenance.source_path,
                                               node.source_range));
            }
        }

        collect_call_and_effect_event_defines();
        collect_called_observation_defines();
    }

    void collect_assignments() {
        for (const auto &[name, agent] : agents_) {
            (void)name;
            if (agent_has_contract(agent.get())) {
                collect_state_machine_assignments(agent.get(),
                                                  agent_state_var(agent.get()),
                                                  build_effective_adjacency(agent.get()));
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

    void collect_specs() {
        for (const auto &contract : contracts_) {
            const auto agent = find_agent(contract.get().target);
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

                specs_.push_back("-- contract " + contract.get().target + " " +
                                 contract_clause_kind_name(clause.kind) + "[" +
                                 std::to_string(index) + "]");
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

    [[nodiscard]] MaybeCRef<ir::AgentDecl> find_agent(std::string_view canonical_name) const {
        if (const auto iter = agents_.find(std::string(canonical_name)); iter != agents_.end()) {
            return std::cref(iter->second.get());
        }

        return std::nullopt;
    }

    [[nodiscard]] bool agent_has_contract(const ir::AgentDecl &agent) const {
        return std::ranges::any_of(
            contracts_, [&](const auto &contract) { return contract.get().target == agent.name; });
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

    [[nodiscard]] MaybeCRef<ir::CapabilityDecl>
    find_capability(std::string_view canonical_name) const {
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

    void add_symbol_mapping(const std::string &symbol, const std::string &description) {
        const auto mapping = symbol + " => " + description;
        if (std::ranges::find(symbol_mappings_, mapping) == symbol_mappings_.end()) {
            symbol_mappings_.push_back(mapping);
        }
    }

    [[nodiscard]] std::string with_source(std::string description,
                                          std::string_view source_path,
                                          const ir::SourceRangeOpt &range) const {
        description += source_suffix(source_path, range);
        return description;
    }

    [[nodiscard]] std::string contract_clause_source_suffix(std::string_view owner,
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

    [[nodiscard]] std::string workflow_clause_source_suffix(std::string_view owner,
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

    [[nodiscard]] std::string
    observation_source_suffix(const ir::FormalObservationScope &scope) const {
        if (scope.kind == ir::FormalObservationScopeKind::ContractClause) {
            return contract_clause_source_suffix(scope.owner, scope.clause_index);
        }

        return workflow_clause_source_suffix(scope.owner, scope.kind, scope.clause_index);
    }

    [[nodiscard]] const ir::Expr *find_embedded_expr_by_atom(const ir::TemporalExpr &expr,
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

    [[nodiscard]] const ir::Expr *
    find_embedded_observation_expr(const ir::FormalObservationScope &scope) const {
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

    [[nodiscard]] bool
    embedded_observation_requires_variable(const ir::FormalObservationScope &scope) const {
        const auto *expr = find_embedded_observation_expr(scope);
        return expr == nullptr || !render_bounded_expr(*expr).has_value();
    }

    [[nodiscard]] bool handler_calls_capability(const ir::StateHandler &handler,
                                                std::string_view capability_name) const {
        return std::ranges::any_of(handler.summary.called_targets, [&](const auto &target) {
            return capability_name_matches(target, capability_name);
        });
    }

    [[nodiscard]] std::string
    render_workflow_node_state_guard(const ir::WorkflowDecl &workflow,
                                     const ir::WorkflowNode &node,
                                     const ir::StateHandler &handler) const {
        return "(" + workflow_node_running_var(workflow, node.name) + " & (" +
               workflow_node_state_var(workflow, node.name) + " = " + handler.state_name + "))";
    }

    [[nodiscard]] std::vector<std::string>
    collect_called_guards(std::string_view agent_name, std::string_view capability_name) const {
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

    void collect_call_and_effect_event_defines() {
        std::map<std::string, std::vector<std::string>> call_guards;
        std::map<std::string, std::vector<std::string>> effect_guards;
        std::map<std::string, std::string> event_failure_vars;
        std::map<std::string, std::string> call_mappings;
        std::map<std::string, std::string> effect_mappings;

        for (const auto &workflow : workflows_) {
            for (const auto &node : workflow.get().nodes) {
                const auto flow = find_flow(node.target);
                if (!flow.has_value()) {
                    continue;
                }

                for (const auto &handler : flow->get().state_handlers) {
                    const auto guard =
                        render_workflow_node_state_guard(workflow.get(), node, handler);
                    const auto failure_var =
                        workflow_node_failure_requested_var(workflow.get(), node.name);
                    const auto source =
                        source_suffix(flow->get().provenance.source_path, handler.source_range);
                    for (const auto &called_target : handler.summary.called_targets) {
                        const auto capability = find_capability(called_target);
                        if (!capability.has_value()) {
                            continue;
                        }

                        const auto call_symbol =
                            workflow_node_call_event_var(workflow.get(), node.name, called_target);
                        call_guards[call_symbol].push_back(guard);
                        event_failure_vars.emplace(call_symbol, failure_var);
                        call_mappings.emplace(call_symbol,
                                              "workflow " + workflow.get().name + " node " +
                                                  node.name + " calls " + called_target + source);

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
                defines_.push_back(committed + " := (" + symbol + " & !" + failure_var->second +
                                   ");");
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
                defines_.push_back(committed + " := (" + symbol + " & !" + failure_var->second +
                                   ");");
                defines_.push_back(failed + " := (" + symbol + " & " + failure_var->second + ");");
                add_symbol_mapping(committed, effect_mappings.at(symbol) + " committed");
                add_symbol_mapping(failed, effect_mappings.at(symbol) + " failed");
            }
        }
    }

    void collect_called_observation_defines() {
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
                symbol + " := " +
                render_disjunction(collect_called_guards(agent_name, capability_name)) + ";");
        }
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
        const auto phase_var = workflow_node_phase_var(workflow, node.name);
        const auto failure_requested_var = workflow_node_failure_requested_var(workflow, node.name);
        const auto dependency_guard = render_dependency_guard(workflow, node.after);
        const auto final_guard = render_state_membership(state_var, agent.final_states);

        collect_state_machine_assignments(agent,
                                          state_var,
                                          build_effective_adjacency(agent),
                                          "!(" + phase_var + " = " + std::string(kNodeRunning) +
                                              ")");

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

    void collect_workflow_control_specs(
        const ir::WorkflowDecl &workflow,
        const std::unordered_map<std::string, WorkflowNodeInfo> &node_map) {
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

    void add_effect_obligation_spec(const ir::WorkflowDecl &workflow,
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

    void add_event_lifecycle_specs(const ir::WorkflowDecl &workflow,
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

    void add_failed_effect_recovery_spec(const ir::WorkflowDecl &workflow,
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

    void collect_effect_obligation_specs() {
        for (const auto &workflow : workflows_) {
            for (const auto &node : workflow.get().nodes) {
                const auto flow = find_flow(node.target);
                if (!flow.has_value()) {
                    continue;
                }

                for (const auto &handler : flow->get().state_handlers) {
                    for (const auto &called_target : handler.summary.called_targets) {
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

                        add_effect_obligation_spec(workflow.get(),
                                                   node,
                                                   called_target,
                                                   call_event,
                                                   "effect[declared]",
                                                   true);

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

    [[nodiscard]] std::string wrap_formula_with_observation_assumptions(
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

    [[nodiscard]] std::string
    wrap_formula_with_workflow_no_failure_assumption(const ir::WorkflowDecl &workflow,
                                                     std::string formula) const {
        std::vector<std::string> no_failure_guards;
        no_failure_guards.reserve(workflow.nodes.size());

        for (const auto &node : workflow.nodes) {
            no_failure_guards.push_back("!" +
                                        workflow_node_failure_requested_var(workflow, node.name));
        }

        if (no_failure_guards.empty()) {
            return formula;
        }

        return "(G (" + render_conjunction(no_failure_guards) + ") -> (" + formula + "))";
    }

    [[nodiscard]] std::string
    render_agent_formula(const ir::TemporalExpr &expr,
                         const ir::AgentDecl &agent,
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
                                      ir::FormalObservationScopeKind::ContractClause,
                                      agent.name,
                                      clause_index,
                                      atom_index++);
                                  observation_assumptions.push_back(observation);
                                  return observation;
                              },
                              [&](const ir::CalledTemporalExpr &value) {
                                  return lookup_called_observation_symbol(agent.name,
                                                                          value.capability);
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
                                         render_agent_formula(*value.lhs,
                                                              agent,
                                                              clause_index,
                                                              atom_index,
                                                              observation_assumptions) +
                                         " " + smv_binary_op(value.op) + " " +
                                         render_agent_formula(*value.rhs,
                                                              agent,
                                                              clause_index,
                                                              atom_index,
                                                              observation_assumptions) +
                                         ")";
                              },
                              [&](const auto &) { return std::string("FALSE"); },
                          },
                          expr.node);
    }

    [[nodiscard]] std::optional<std::string>
    render_contract_expr_clause(const ir::ContractDecl &contract,
                                const ir::AgentDecl &agent,
                                ir::ContractClauseKind kind,
                                const ir::Expr &expr,
                                std::size_t clause_index) {
        const auto bounded_expr = render_bounded_expr(expr);
        if (bounded_expr.has_value()) {
            switch (kind) {
            case ir::ContractClauseKind::Requires:
                specs_.push_back("-- contract " + contract.target + " requires[" +
                                 std::to_string(clause_index) +
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
            ir::FormalObservationScopeKind::ContractClause, contract.target, clause_index, 0);

        switch (kind) {
        case ir::ContractClauseKind::Requires:
            specs_.push_back("-- contract " + contract.target + " requires[" +
                             std::to_string(clause_index) +
                             "] observation_assumption: " + observation);
            return std::nullopt;
        case ir::ContractClauseKind::Ensures:
            specs_.push_back("-- contract " + contract.target + " ensures[" +
                             std::to_string(clause_index) +
                             "] data_guarantee_abstracted: " + observation);
            return std::nullopt;
        case ir::ContractClauseKind::Invariant:
        case ir::ContractClauseKind::Forbid:
            specs_.push_back("-- contract " + contract.target + " " +
                             contract_clause_kind_name(kind) + "[" + std::to_string(clause_index) +
                             "] observation_assumption: " + observation);
            return std::nullopt;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string
    render_workflow_formula(const ir::TemporalExpr &expr,
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

    void emit() {
        out_ << "-- AHFL restricted SMV backend v0.1\n";
        out_ << "-- This lowering preserves validated state-machine, flow, and workflow "
                "structure.\n";
        out_ << "-- Bounded boolean/integer predicates are lowered directly; other data predicates "
                "remain environment observation assumptions.\n";
        out_ << "-- Capability calls/effects are bound to flow handler events where available.\n";
        for (const auto &mapping : symbol_mappings_) {
            out_ << "-- AHFL_MAP " << mapping << '\n';
        }
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

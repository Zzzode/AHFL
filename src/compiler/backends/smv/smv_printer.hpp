#pragma once

#include <algorithm>
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

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "base/support/overloaded.hpp"
#include "base/support/string_utils.hpp"
#include "compiler/backends/smv/smv_helpers.hpp"
#include "compiler/backends/smv/smv_naming.hpp"

namespace ahfl {

struct SmvWorkflowNodeInfo {
    std::reference_wrapper<const ir::WorkflowNode> node;
    std::reference_wrapper<const ir::AgentDecl> agent;
};

class SmvPrinter final {
  public:
    explicit SmvPrinter(std::ostream &out) : out_(out) {}

    void print(const ir::Program &program);

  private:
    std::ostream &out_;
    const ir::Program *program_{nullptr};
    ir::ProgramIndex index_;
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

    // --- Indexing (smv_indexing.cpp) ---
    void index_declarations(const ir::Program &program);
    void index_observations(const ir::Program &program);
    [[nodiscard]] MaybeCRef<ir::AgentDecl> find_agent(std::string_view canonical_name) const;
    [[nodiscard]] bool agent_has_contract(const ir::AgentDecl &agent) const;
    [[nodiscard]] std::unordered_map<std::string, SmvWorkflowNodeInfo>
    build_workflow_node_map(const ir::WorkflowDecl &workflow) const;
    [[nodiscard]] MaybeCRef<ir::FlowDecl> find_flow(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<ir::CapabilityDecl>
    find_capability(std::string_view canonical_name) const;
    void add_symbol_mapping(const std::string &symbol, const std::string &description);
    [[nodiscard]] std::string with_source(std::string description,
                                          std::string_view source_path,
                                          const ir::SourceRangeOpt &range) const;
    [[nodiscard]] std::string contract_clause_source_suffix(std::string_view owner,
                                                            std::size_t clause_index) const;
    [[nodiscard]] std::string workflow_clause_source_suffix(std::string_view owner,
                                                            ir::FormalObservationScopeKind kind,
                                                            std::size_t clause_index) const;
    [[nodiscard]] std::string
    observation_source_suffix(const ir::FormalObservationScope &scope) const;
    [[nodiscard]] const ir::Expr *find_embedded_expr_by_atom(const ir::TemporalExpr &expr,
                                                             std::size_t target_atom,
                                                             std::size_t &current_atom) const;
    [[nodiscard]] const ir::Expr *
    find_embedded_observation_expr(const ir::FormalObservationScope &scope) const;
    [[nodiscard]] bool
    embedded_observation_requires_variable(const ir::FormalObservationScope &scope) const;
    [[nodiscard]] bool handler_calls_capability(const ir::StateHandler::Summary &summary,
                                                std::string_view capability_name) const;
    [[nodiscard]] std::string
    render_workflow_node_state_guard(const ir::WorkflowDecl &workflow,
                                     const ir::WorkflowNode &node,
                                     const ir::StateHandler &handler) const;
    [[nodiscard]] std::vector<std::string>
    collect_called_guards(std::string_view agent_name, std::string_view capability_name) const;

    // --- Collection (smv_collection.cpp) ---
    void collect_state_variables();
    void collect_defines();
    void collect_assignments();
    void collect_call_and_effect_event_defines();
    void collect_called_observation_defines();
    [[nodiscard]] std::string
    lookup_called_observation_symbol(std::string_view agent_name,
                                     std::string_view capability_name) const;
    [[nodiscard]] std::string
    lookup_embedded_observation_symbol(ir::FormalObservationScopeKind kind,
                                       std::string_view owner,
                                       std::size_t clause_index,
                                       std::size_t atom_index) const;
    void collect_workflow_node_assignments(const ir::WorkflowDecl &workflow,
                                           const ir::WorkflowNode &node,
                                           const ir::AgentDecl &agent);
    void collect_state_machine_assignments(
        const ir::AgentDecl &agent,
        std::string state_var,
        const std::unordered_map<std::string, std::vector<std::string>> &adjacency,
        std::string block_guard = {});
    [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
    build_effective_adjacency(const ir::AgentDecl &agent) const;
    [[nodiscard]] std::string render_next_targets(const std::vector<std::string> &targets,
                                                  std::string_view fallback_state) const;
    [[nodiscard]] std::string render_dependency_guard(const ir::WorkflowDecl &workflow,
                                                      const std::vector<std::string> &after) const;

    // --- Specs (smv_specs.cpp) ---
    void collect_specs();
    void collect_workflow_control_specs(
        const ir::WorkflowDecl &workflow,
        const std::unordered_map<std::string, SmvWorkflowNodeInfo> &node_map);
    void add_effect_obligation_spec(const ir::WorkflowDecl &workflow,
                                    const ir::WorkflowNode &node,
                                    std::string_view called_target,
                                    std::string_view event_symbol,
                                    std::string_view obligation_name,
                                    bool satisfied);
    void add_event_lifecycle_specs(const ir::WorkflowDecl &workflow,
                                   const ir::WorkflowNode &node,
                                   std::string_view event_name,
                                   std::string_view event_symbol,
                                   std::string_view committed_symbol,
                                   std::string_view failed_symbol);
    void add_failed_effect_recovery_spec(const ir::WorkflowDecl &workflow,
                                         const ir::WorkflowNode &node,
                                         std::string_view failed_symbol);
    void collect_effect_obligation_specs();

    // --- Formula (smv_formula.cpp) ---
    [[nodiscard]] std::string wrap_formula_with_observation_assumptions(
        std::string formula, const std::vector<std::string> &observation_assumptions) const;
    [[nodiscard]] std::string
    wrap_formula_with_workflow_no_failure_assumption(const ir::WorkflowDecl &workflow,
                                                     std::string formula) const;
    [[nodiscard]] std::string
    render_agent_formula(const ir::TemporalExpr &expr,
                         const ir::AgentDecl &agent,
                         std::size_t clause_index,
                         std::size_t &atom_index,
                         std::vector<std::string> &observation_assumptions) const;
    [[nodiscard]] std::optional<std::string>
    render_contract_expr_clause(const ir::ContractDecl &contract,
                                const ir::AgentDecl &agent,
                                ir::ContractClauseKind kind,
                                const ir::Expr &expr,
                                std::size_t clause_index);
    [[nodiscard]] std::string
    render_workflow_formula(const ir::TemporalExpr &expr,
                            const ir::WorkflowDecl &workflow,
                            ir::FormalObservationScopeKind scope_kind,
                            std::size_t clause_index,
                            std::size_t &atom_index,
                            std::vector<std::string> &observation_assumptions) const;

    // --- Emit (smv.cpp) ---
    void emit();
};

} // namespace ahfl

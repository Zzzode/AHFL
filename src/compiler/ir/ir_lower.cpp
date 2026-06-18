#include "ahfl/compiler/ir/lowering.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "base/support/overloaded.hpp"
#include "base/support/string_utils.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::string called_observation_symbol(std::string_view agent_name,
                                                    std::string_view capability_name) {
    return "agent__" + sanitize_identifier(agent_name) + "__called__" +
           sanitize_identifier(capability_name);
}

[[nodiscard]] std::string observation_scope_base_name(const ir::FormalObservationScope &scope) {
    switch (scope.kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract__" + scope.owner + "__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow__" + scope.owner + "__safety__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow__" + scope.owner + "__liveness__" + std::to_string(scope.clause_index);
    }

    return "observation";
}

[[nodiscard]] std::string embedded_observation_symbol(const ir::FormalObservationScope &scope) {
    return sanitize_identifier(observation_scope_base_name(scope) + "__atom__" +
                               std::to_string(scope.atom_index));
}

class FormalObservationCollector final {
  public:
    [[nodiscard]] std::vector<ir::FormalObservation> collect(const ir::Program &program) {
        observations_.clear();
        observation_index_by_symbol_.clear();

        for (const auto &declaration : program.declarations) {
            std::visit(Overloaded{
                           [this](const ir::AgentDecl &agent) { collect_agent(agent); },
                           [this](const ir::ContractDecl &contract) { collect_contract(contract); },
                           [this](const ir::WorkflowDecl &workflow) { collect_workflow(workflow); },
                           [](const auto &) {},
                       },
                       declaration);
        }

        return observations_;
    }

  private:
    std::vector<ir::FormalObservation> observations_;
    std::unordered_map<std::string, std::size_t> observation_index_by_symbol_;

    void collect_agent(const ir::AgentDecl &agent) {
        for (const auto &capability : agent.capability_refs) {
            const auto capability_name = ir::symbol_canonical_name(capability);
            if (!capability_name.empty()) {
                add_called_observation(agent.name, capability_name);
            }
        }
    }

    void collect_contract(const ir::ContractDecl &contract) {
        const auto target = std::string(ir::symbol_canonical_name(contract.target_ref));
        for (std::size_t clause_index = 0; clause_index < contract.clauses.size(); ++clause_index) {
            const auto expr = std::get_if<ir::ExprRef>(&contract.clauses[clause_index].value);
            if (expr != nullptr) {
                add_embedded_observation(ir::FormalObservationScope{
                    .kind = ir::FormalObservationScopeKind::ContractClause,
                    .owner = target,
                    .clause_index = clause_index,
                    .atom_index = 0,
                });
                continue;
            }

            const auto temporal =
                std::get_if<ir::TemporalExprPtr>(&contract.clauses[clause_index].value);
            if (temporal != nullptr) {
                std::size_t atom_index = 0;
                collect_contract_formula(**temporal, target, clause_index, atom_index);
            }
        }
    }

    void collect_workflow(const ir::WorkflowDecl &workflow) {
        for (std::size_t clause_index = 0; clause_index < workflow.safety.size(); ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.safety[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }

        for (std::size_t clause_index = 0; clause_index < workflow.liveness.size();
             ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.liveness[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }
    }

    void collect_contract_formula(const ir::TemporalExpr &expr,
                                  std::string_view agent_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    add_embedded_observation(ir::FormalObservationScope{
                        .kind = ir::FormalObservationScopeKind::ContractClause,
                        .owner = std::string(agent_name),
                        .clause_index = clause_index,
                        .atom_index = atom_index++,
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    add_called_observation(agent_name, value.capability);
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    collect_contract_formula(*value.operand, agent_name, clause_index, atom_index);
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    collect_contract_formula(*value.lhs, agent_name, clause_index, atom_index);
                    collect_contract_formula(*value.rhs, agent_name, clause_index, atom_index);
                },
                [&](const auto &) {},
            },
            expr.node);
    }

    void collect_workflow_formula(const ir::TemporalExpr &expr,
                                  ir::FormalObservationScopeKind scope_kind,
                                  std::string_view workflow_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(Overloaded{
                       [&](const ir::EmbeddedTemporalExpr &) {
                           add_embedded_observation(ir::FormalObservationScope{
                               .kind = scope_kind,
                               .owner = std::string(workflow_name),
                               .clause_index = clause_index,
                               .atom_index = atom_index++,
                           });
                       },
                       [&](const ir::TemporalUnaryExpr &value) {
                           collect_workflow_formula(
                               *value.operand, scope_kind, workflow_name, clause_index, atom_index);
                       },
                       [&](const ir::TemporalBinaryExpr &value) {
                           collect_workflow_formula(
                               *value.lhs, scope_kind, workflow_name, clause_index, atom_index);
                           collect_workflow_formula(
                               *value.rhs, scope_kind, workflow_name, clause_index, atom_index);
                       },
                       [&](const auto &) {},
                   },
                   expr.node);
    }

    void add_called_observation(std::string_view agent_name, std::string_view capability_name) {
        const auto symbol = called_observation_symbol(agent_name, capability_name);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node =
                ir::CalledCapabilityObservation{
                    .agent = std::string(agent_name),
                    .capability = std::string(capability_name),
                },
        });
    }

    void add_embedded_observation(ir::FormalObservationScope scope) {
        const auto symbol = embedded_observation_symbol(scope);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node =
                ir::EmbeddedBoolObservation{
                    .scope = std::move(scope),
                },
        });
    }
};

} // namespace

ir::Program lower_program_ir(const ast::Program &program,
                             const ResolveResult &,
                             const TypeCheckResult &type_check_result) {
    return lower_typed_program(type_check_result.typed_program, program);
}

ir::Program lower_program_ir(const SourceGraph &graph,
                             const ResolveResult &,
                             const TypeCheckResult &type_check_result) {
    return lower_typed_program(type_check_result.typed_program, graph);
}

std::vector<ir::FormalObservation> collect_formal_observations(const ir::Program &program) {
    FormalObservationCollector collector;
    return collector.collect(program);
}

} // namespace ahfl

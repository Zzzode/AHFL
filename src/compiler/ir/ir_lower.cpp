#include "ahfl/compiler/ir/lowering.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
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
            // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per Decl alternative,
            // generated from decl_nodes.def. Only agent / contract / workflow
            // declarations carry formal observations; every other declaration
            // names OBSERVE_DECL_LEAF (explicit, named no-op — the same behavior
            // the unnamed catch-all had). The enumeration turns a NEW declaration
            // node into a COMPILE ERROR here until it is classified.
            //
            // The leaf captures nothing: a `[this]` capture the body never names
            // is -Wunused-lambda-capture under clang (-Werror here), so every
            // leaf expansion would break the build. Only the handlers that
            // actually call a member keep the capture.
#define OBSERVE_DECL_LEAF(Name) [](const ir::Name &) {},
#define OBSERVE_AgentDecl(Name) [this](const ir::Name &agent) { collect_agent(agent); },
#define OBSERVE_ContractDecl(Name)                                                              \
    [this](const ir::Name &contract) { collect_contract(contract); },
#define OBSERVE_WorkflowDecl(Name)                                                              \
    [this](const ir::Name &workflow) { collect_workflow(workflow); },
#define OBSERVE_ModuleDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_ImportDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_ConstDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_TypeAliasDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_StructDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_EnumDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_CapabilityDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_PredicateDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_FlowDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_FnDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_TraitDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_ImplDecl(Name) OBSERVE_DECL_LEAF(Name)
#define OBSERVE_InstanceDecl(Name) OBSERVE_DECL_LEAF(Name)
#define HANDLE_DECL_NODE(Name, Wire) OBSERVE_##Name(Name)
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/decl_nodes.def"
                },
                declaration);
#undef HANDLE_DECL_NODE
#undef OBSERVE_ModuleDecl
#undef OBSERVE_ImportDecl
#undef OBSERVE_ConstDecl
#undef OBSERVE_TypeAliasDecl
#undef OBSERVE_StructDecl
#undef OBSERVE_EnumDecl
#undef OBSERVE_CapabilityDecl
#undef OBSERVE_PredicateDecl
#undef OBSERVE_AgentDecl
#undef OBSERVE_ContractDecl
#undef OBSERVE_FlowDecl
#undef OBSERVE_WorkflowDecl
#undef OBSERVE_FnDecl
#undef OBSERVE_TraitDecl
#undef OBSERVE_ImplDecl
#undef OBSERVE_InstanceDecl
#undef OBSERVE_DECL_LEAF
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
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per TemporalExprNode
        // alternative, generated from temporal_nodes.def. `called` / `embedded` /
        // unary / binary atoms are indexed above; the in_state / running /
        // completed atoms name OBSERVE_ATOM_LEAF (explicit, named no-op — the
        // same behavior the unnamed catch-all had). The enumeration turns a NEW
        // temporal node into a COMPILE ERROR here until it is classified.
#define OBSERVE_ATOM_LEAF(Name) [&](const ir::Name &) {},
#define OBSERVE_EmbeddedTemporalExpr(Name)                                                      \
    [&](const ir::Name &) {                                                                     \
        add_embedded_observation(ir::FormalObservationScope{                                    \
            .kind = ir::FormalObservationScopeKind::ContractClause,                             \
            .owner = std::string(agent_name),                                                   \
            .clause_index = clause_index,                                                       \
            .atom_index = atom_index++,                                                         \
        });                                                                                     \
    },
#define OBSERVE_CalledTemporalExpr(Name)                                                        \
    [&](const ir::Name &value) { add_called_observation(agent_name, value.capability); },
#define OBSERVE_TemporalUnaryExpr(Name)                                                         \
    [&](const ir::Name &value) {                                                                \
        collect_contract_formula(*value.operand, agent_name, clause_index, atom_index);         \
    },
#define OBSERVE_TemporalBinaryExpr(Name)                                                        \
    [&](const ir::Name &value) {                                                                \
        collect_contract_formula(*value.lhs, agent_name, clause_index, atom_index);             \
        collect_contract_formula(*value.rhs, agent_name, clause_index, atom_index);             \
    },
#define OBSERVE_InStateTemporalExpr(Name) OBSERVE_ATOM_LEAF(Name)
#define OBSERVE_RunningTemporalExpr(Name) OBSERVE_ATOM_LEAF(Name)
#define OBSERVE_CompletedTemporalExpr(Name) OBSERVE_ATOM_LEAF(Name)
#define HANDLE_TEMPORAL_NODE(Name, Wire) OBSERVE_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/temporal_nodes.def"
            },
            expr.node);
#undef HANDLE_TEMPORAL_NODE
#undef OBSERVE_EmbeddedTemporalExpr
#undef OBSERVE_CalledTemporalExpr
#undef OBSERVE_InStateTemporalExpr
#undef OBSERVE_RunningTemporalExpr
#undef OBSERVE_CompletedTemporalExpr
#undef OBSERVE_TemporalUnaryExpr
#undef OBSERVE_TemporalBinaryExpr
#undef OBSERVE_ATOM_LEAF
    }

    void collect_workflow_formula(const ir::TemporalExpr &expr,
                                  ir::FormalObservationScopeKind scope_kind,
                                  std::string_view workflow_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per TemporalExprNode
        // alternative, generated from temporal_nodes.def. A workflow-scoped
        // clause can only index an embedded bool observation plus the unary /
        // binary connectives; the called / in_state / running / completed atoms
        // name OBSERVE_WF_LEAF (explicit, named no-op — the same behavior the
        // unnamed catch-all had). The enumeration turns a NEW temporal node into
        // a COMPILE ERROR here until it is classified.
#define OBSERVE_WF_LEAF(Name) [&](const ir::Name &) {},
#define OBSERVE_WF_EmbeddedTemporalExpr(Name)                                                   \
    [&](const ir::Name &) {                                                                     \
        add_embedded_observation(ir::FormalObservationScope{                                    \
            .kind = scope_kind,                                                                 \
            .owner = std::string(workflow_name),                                                \
            .clause_index = clause_index,                                                       \
            .atom_index = atom_index++,                                                         \
        });                                                                                     \
    },
#define OBSERVE_WF_TemporalUnaryExpr(Name)                                                      \
    [&](const ir::Name &value) {                                                                \
        collect_workflow_formula(                                                               \
            *value.operand, scope_kind, workflow_name, clause_index, atom_index);               \
    },
#define OBSERVE_WF_TemporalBinaryExpr(Name)                                                     \
    [&](const ir::Name &value) {                                                                \
        collect_workflow_formula(                                                               \
            *value.lhs, scope_kind, workflow_name, clause_index, atom_index);                   \
        collect_workflow_formula(                                                               \
            *value.rhs, scope_kind, workflow_name, clause_index, atom_index);                   \
    },
#define OBSERVE_WF_CalledTemporalExpr(Name) OBSERVE_WF_LEAF(Name)
#define OBSERVE_WF_InStateTemporalExpr(Name) OBSERVE_WF_LEAF(Name)
#define OBSERVE_WF_RunningTemporalExpr(Name) OBSERVE_WF_LEAF(Name)
#define OBSERVE_WF_CompletedTemporalExpr(Name) OBSERVE_WF_LEAF(Name)
#define HANDLE_TEMPORAL_NODE(Name, Wire) OBSERVE_WF_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/temporal_nodes.def"
            },
            expr.node);
#undef HANDLE_TEMPORAL_NODE
#undef OBSERVE_WF_EmbeddedTemporalExpr
#undef OBSERVE_WF_CalledTemporalExpr
#undef OBSERVE_WF_InStateTemporalExpr
#undef OBSERVE_WF_RunningTemporalExpr
#undef OBSERVE_WF_CompletedTemporalExpr
#undef OBSERVE_WF_TemporalUnaryExpr
#undef OBSERVE_WF_TemporalBinaryExpr
#undef OBSERVE_WF_LEAF
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
                             const TypeCheckResult &type_check_result,
                             bool include_stdlib) {
    return lower_typed_program(type_check_result.typed_program, graph, include_stdlib);
}

std::vector<ir::FormalObservation> collect_formal_observations(const ir::Program &program) {
    FormalObservationCollector collector;
    return collector.collect(program);
}

} // namespace ahfl

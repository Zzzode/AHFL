#include "ahfl/handoff/package.hpp"

#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ahfl::handoff {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct FormalObservationScopeKey {
    ir::FormalObservationScopeKind kind{ir::FormalObservationScopeKind::ContractClause};
    std::string owner;
    std::size_t clause_index{0};

    [[nodiscard]] friend bool operator==(const FormalObservationScopeKey &lhs,
                                         const FormalObservationScopeKey &rhs) noexcept = default;
};

struct FormalObservationScopeKeyHash {
    [[nodiscard]] std::size_t operator()(const FormalObservationScopeKey &value) const noexcept {
        const auto kind_hash =
            std::hash<int>{}(static_cast<int>(value.kind));
        const auto owner_hash = std::hash<std::string>{}(value.owner);
        const auto index_hash = std::hash<std::size_t>{}(value.clause_index);
        return kind_hash ^ (owner_hash << 1U) ^ (index_hash << 2U);
    }
};

void push_unique_target_ref(std::vector<ExecutableRef> &values, const ExecutableRef &value) {
    for (const auto &existing : values) {
        if (existing == value) {
            return;
        }
    }

    values.push_back(value);
}

CapabilityBindingSlot &
get_or_create_capability_binding_slot(std::vector<CapabilityBindingSlot> &slots,
                                      const PackageMetadata &metadata,
                                      std::string_view capability_name) {
    for (auto &slot : slots) {
        if (slot.capability_name == capability_name) {
            return slot;
        }
    }

    auto binding_key = std::string(capability_name);
    if (const auto found = metadata.capability_binding_keys.find(std::string(capability_name));
        found != metadata.capability_binding_keys.end()) {
        binding_key = found->second;
    }

    slots.push_back(CapabilityBindingSlot{
        .capability_name = std::string(capability_name),
        .binding_key = std::move(binding_key),
    });
    return slots.back();
}

AgentExecutable lower_agent_executable(const ir::AgentDecl &declaration) {
    return AgentExecutable{
        .provenance = declaration.provenance,
        .canonical_name = declaration.name,
        .input_type = declaration.input_type,
        .context_type = declaration.context_type,
        .output_type = declaration.output_type,
        .states = declaration.states,
        .initial_state = declaration.initial_state,
        .final_states = declaration.final_states,
        .capabilities = declaration.capabilities,
    };
}

WorkflowNodeLifecycleSummary
lower_workflow_node_lifecycle(const ir::WorkflowNode &node, const ir::AgentDecl *target_agent) {
    WorkflowNodeLifecycleSummary lifecycle{
        .start_condition = node.after.empty() ? WorkflowNodeStartConditionKind::Immediate
                                              : WorkflowNodeStartConditionKind::AfterDependenciesCompleted,
    };

    if (target_agent == nullptr) {
        return lifecycle;
    }

    lifecycle.target_initial_state = target_agent->initial_state;
    lifecycle.target_final_states = target_agent->final_states;
    return lifecycle;
}

WorkflowExecutionGraph lower_workflow_execution_graph(const ir::WorkflowDecl &declaration) {
    WorkflowExecutionGraph graph;
    graph.entry_nodes.reserve(declaration.nodes.size());

    std::unordered_set<std::string> seen_entry_nodes;
    for (const auto &node : declaration.nodes) {
        if (node.after.empty() && seen_entry_nodes.emplace(node.name).second) {
            graph.entry_nodes.push_back(node.name);
        }

        for (const auto &dependency : node.after) {
            graph.dependency_edges.push_back(WorkflowDependencyEdge{
                .from_node = dependency,
                .to_node = node.name,
            });
        }
    }

    return graph;
}

[[nodiscard]] PolicyObligationKind policy_obligation_kind(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return PolicyObligationKind::Requires;
    case ir::ContractClauseKind::Ensures:
        return PolicyObligationKind::Ensures;
    case ir::ContractClauseKind::Invariant:
        return PolicyObligationKind::Invariant;
    case ir::ContractClauseKind::Forbid:
        return PolicyObligationKind::Forbid;
    }

    return PolicyObligationKind::Requires;
}

[[nodiscard]] std::unordered_map<FormalObservationScopeKey, std::vector<std::string>, FormalObservationScopeKeyHash>
index_formal_observation_symbols(const std::vector<ir::FormalObservation> &observations) {
    std::unordered_map<FormalObservationScopeKey, std::vector<std::string>, FormalObservationScopeKeyHash>
        index;

    for (const auto &observation : observations) {
        if (const auto *embedded = std::get_if<ir::EmbeddedBoolObservation>(&observation.node);
            embedded != nullptr) {
            index[FormalObservationScopeKey{
                .kind = embedded->scope.kind,
                .owner = embedded->scope.owner,
                .clause_index = embedded->scope.clause_index,
            }]
                .push_back(observation.symbol);
        }
    }

    return index;
}

void add_contract_policy_obligations(
    std::vector<PolicyObligation> &obligations,
    const ir::ContractDecl &contract,
    const std::unordered_map<FormalObservationScopeKey,
                             std::vector<std::string>,
                             FormalObservationScopeKeyHash> &observation_index) {
    for (std::size_t clause_index = 0; clause_index < contract.clauses.size(); ++clause_index) {
        const auto scope_key = FormalObservationScopeKey{
            .kind = ir::FormalObservationScopeKind::ContractClause,
            .owner = contract.target,
            .clause_index = clause_index,
        };

        auto observation_symbols = [&]() {
            const auto found = observation_index.find(scope_key);
            return found == observation_index.end() ? std::vector<std::string>{} : found->second;
        }();

        obligations.push_back(PolicyObligation{
            .owner_target =
                ExecutableRef{
                    .kind = ExecutableKind::Agent,
                    .canonical_name = contract.target,
                },
            .kind = policy_obligation_kind(contract.clauses[clause_index].kind),
            .clause_index = clause_index,
            .observation_symbols = std::move(observation_symbols),
        });
    }
}

void add_workflow_policy_obligations(
    std::vector<PolicyObligation> &obligations,
    const ir::WorkflowDecl &workflow,
    const std::unordered_map<FormalObservationScopeKey,
                             std::vector<std::string>,
                             FormalObservationScopeKeyHash> &observation_index) {
    const auto owner_target = ExecutableRef{
        .kind = ExecutableKind::Workflow,
        .canonical_name = workflow.name,
    };

    for (std::size_t clause_index = 0; clause_index < workflow.safety.size(); ++clause_index) {
        const auto scope_key = FormalObservationScopeKey{
            .kind = ir::FormalObservationScopeKind::WorkflowSafetyClause,
            .owner = workflow.name,
            .clause_index = clause_index,
        };

        auto observation_symbols = [&]() {
            const auto found = observation_index.find(scope_key);
            return found == observation_index.end() ? std::vector<std::string>{} : found->second;
        }();

        obligations.push_back(PolicyObligation{
            .owner_target = owner_target,
            .kind = PolicyObligationKind::WorkflowSafety,
            .clause_index = clause_index,
            .observation_symbols = std::move(observation_symbols),
        });
    }

    for (std::size_t clause_index = 0; clause_index < workflow.liveness.size(); ++clause_index) {
        const auto scope_key = FormalObservationScopeKey{
            .kind = ir::FormalObservationScopeKind::WorkflowLivenessClause,
            .owner = workflow.name,
            .clause_index = clause_index,
        };

        auto observation_symbols = [&]() {
            const auto found = observation_index.find(scope_key);
            return found == observation_index.end() ? std::vector<std::string>{} : found->second;
        }();

        obligations.push_back(PolicyObligation{
            .owner_target = owner_target,
            .kind = PolicyObligationKind::WorkflowLiveness,
            .clause_index = clause_index,
            .observation_symbols = std::move(observation_symbols),
        });
    }
}

WorkflowExecutable lower_workflow_executable(
    const ir::WorkflowDecl &declaration,
    const std::unordered_map<std::string, const ir::AgentDecl *> &agents_by_name) {
    WorkflowExecutable workflow{
        .provenance = declaration.provenance,
        .canonical_name = declaration.name,
        .input_type = declaration.input_type,
        .output_type = declaration.output_type,
        .execution_graph = lower_workflow_execution_graph(declaration),
        .safety_clause_count = declaration.safety.size(),
        .liveness_clause_count = declaration.liveness.size(),
        .return_summary = declaration.return_summary,
    };

    workflow.nodes.reserve(declaration.nodes.size());
    for (const auto &node : declaration.nodes) {
        const auto target_agent = [&]() -> const ir::AgentDecl * {
            const auto found = agents_by_name.find(node.target);
            return found == agents_by_name.end() ? nullptr : found->second;
        }();

        workflow.nodes.push_back(WorkflowExecutionNode{
            .name = node.name,
            .target = node.target,
            .input_summary = node.input_summary,
            .after = node.after,
            .lifecycle = lower_workflow_node_lifecycle(node, target_agent),
        });
    }

    return workflow;
}

void add_capability_dependencies_for_agent(std::vector<CapabilityBindingSlot> &slots,
                                           const PackageMetadata &metadata,
                                           const AgentExecutable &agent) {
    const ExecutableRef required_by{
        .kind = ExecutableKind::Agent,
        .canonical_name = agent.canonical_name,
    };

    for (const auto &capability : agent.capabilities) {
        auto &slot = get_or_create_capability_binding_slot(slots, metadata, capability);
        push_unique_target_ref(slot.required_by_targets, required_by);
    }
}

void add_capability_dependencies_for_workflow(
    std::vector<CapabilityBindingSlot> &slots,
    const PackageMetadata &metadata,
    const WorkflowExecutable &workflow,
    const std::unordered_map<std::string, std::vector<std::string>> &agent_capabilities) {
    const ExecutableRef required_by{
        .kind = ExecutableKind::Workflow,
        .canonical_name = workflow.canonical_name,
    };

    for (const auto &node : workflow.nodes) {
        const auto found = agent_capabilities.find(node.target);
        if (found == agent_capabilities.end()) {
            continue;
        }

        for (const auto &capability : found->second) {
            auto &slot = get_or_create_capability_binding_slot(slots, metadata, capability);
            push_unique_target_ref(slot.required_by_targets, required_by);
        }
    }
}

} // namespace

Package lower_package(const ir::Program &program, PackageMetadata metadata) {
    if (metadata.identity.has_value()) {
        metadata.identity->format_version = std::string(kFormatVersion);
    }

    Package package{
        .metadata = std::move(metadata),
        .formal_observations = program.formal_observations,
    };

    std::unordered_map<std::string, std::vector<std::string>> agent_capabilities;
    std::unordered_map<std::string, const ir::AgentDecl *> agents_by_name;
    const auto formal_observation_index = index_formal_observation_symbols(program.formal_observations);
    for (const auto &declaration : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&declaration)) {
            agent_capabilities.emplace(agent->name, agent->capabilities);
            agents_by_name.emplace(agent->name, agent);
        }
    }

    for (const auto &declaration : program.declarations) {
        std::visit(Overloaded{
                       [&](const ir::AgentDecl &value) {
                           auto agent = lower_agent_executable(value);
                           add_capability_dependencies_for_agent(
                               package.capability_binding_slots, package.metadata, agent);
                           package.executable_targets.push_back(std::move(agent));
                       },
                       [&](const ir::WorkflowDecl &value) {
                           auto workflow = lower_workflow_executable(value, agents_by_name);
                           add_capability_dependencies_for_workflow(
                               package.capability_binding_slots,
                               package.metadata,
                               workflow,
                               agent_capabilities);
                           package.executable_targets.push_back(std::move(workflow));
                           add_workflow_policy_obligations(
                               package.policy_obligations, value, formal_observation_index);
                       },
                       [&](const ir::ContractDecl &value) {
                           add_contract_policy_obligations(
                               package.policy_obligations, value, formal_observation_index);
                       },
                       [&](const auto &) {},
                   },
                   declaration);
    }

    return package;
}

const AgentExecutable *find_agent_executable(const Package &package,
                                             std::string_view canonical_name) {
    for (const auto &target : package.executable_targets) {
        if (const auto *agent = std::get_if<AgentExecutable>(&target);
            agent != nullptr && agent->canonical_name == canonical_name) {
            return agent;
        }
    }

    return nullptr;
}

const WorkflowExecutable *find_workflow_executable(const Package &package,
                                                   std::string_view canonical_name) {
    for (const auto &target : package.executable_targets) {
        if (const auto *workflow = std::get_if<WorkflowExecutable>(&target);
            workflow != nullptr && workflow->canonical_name == canonical_name) {
            return workflow;
        }
    }

    return nullptr;
}

const CapabilityBindingSlot *find_capability_binding_slot(const Package &package,
                                                          std::string_view capability_name) {
    for (const auto &slot : package.capability_binding_slots) {
        if (slot.capability_name == capability_name) {
            return &slot;
        }
    }

    return nullptr;
}

const PolicyObligation *find_policy_obligation(const Package &package,
                                               std::string_view owner_canonical_name,
                                               PolicyObligationKind kind,
                                               std::size_t clause_index) {
    for (const auto &obligation : package.policy_obligations) {
        if (obligation.owner_target.canonical_name == owner_canonical_name &&
            obligation.kind == kind && obligation.clause_index == clause_index) {
            return &obligation;
        }
    }

    return nullptr;
}

} // namespace ahfl::handoff

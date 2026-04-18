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

[[nodiscard]] std::string display_name(std::string_view canonical_name) {
    const auto separator = canonical_name.rfind("::");
    if (separator == std::string_view::npos) {
        return std::string(canonical_name);
    }

    return std::string(canonical_name.substr(separator + 2));
}

struct ExecutableIndex {
    std::unordered_set<std::string> canonical_agents;
    std::unordered_set<std::string> canonical_workflows;
    std::unordered_map<std::string, std::vector<std::string>> display_agents;
    std::unordered_map<std::string, std::vector<std::string>> display_workflows;
};

struct CapabilityIndex {
    std::unordered_set<std::string> canonical;
    std::unordered_map<std::string, std::vector<std::string>> display;
};

void add_unique_name(std::unordered_map<std::string, std::vector<std::string>> &index,
                     std::string display,
                     std::string canonical) {
    auto &values = index[std::move(display)];
    for (const auto &existing : values) {
        if (existing == canonical) {
            return;
        }
    }

    values.push_back(std::move(canonical));
}

[[nodiscard]] ExecutableIndex build_executable_index(const ir::Program &program) {
    ExecutableIndex index;

    for (const auto &declaration : program.declarations) {
        std::visit(Overloaded{
                       [&](const ir::AgentDecl &agent) {
                           index.canonical_agents.insert(agent.name);
                           add_unique_name(index.display_agents, display_name(agent.name), agent.name);
                       },
                       [&](const ir::WorkflowDecl &workflow) {
                           index.canonical_workflows.insert(workflow.name);
                           add_unique_name(
                               index.display_workflows, display_name(workflow.name), workflow.name);
                       },
                       [&](const auto &) {},
                   },
                   declaration);
    }

    return index;
}

[[nodiscard]] CapabilityIndex build_capability_index(const ir::Program &program) {
    CapabilityIndex index;

    for (const auto &declaration : program.declarations) {
        if (const auto *capability = std::get_if<ir::CapabilityDecl>(&declaration);
            capability != nullptr) {
            index.canonical.insert(capability->name);
            add_unique_name(index.display, display_name(capability->name), capability->name);
        }
    }

    return index;
}

[[nodiscard]] std::string executable_kind_name(ExecutableKind kind) {
    switch (kind) {
    case ExecutableKind::Agent:
        return "agent";
    case ExecutableKind::Workflow:
        return "workflow";
    }

    return "executable";
}

[[nodiscard]] std::optional<std::string>
resolve_executable_name(const ExecutableIndex &index,
                        ExecutableKind kind,
                        std::string_view raw_name,
                        DiagnosticBag &diagnostics,
                        std::string_view field_name) {
    const auto &canonical =
        kind == ExecutableKind::Agent ? index.canonical_agents : index.canonical_workflows;
    const auto &display =
        kind == ExecutableKind::Agent ? index.display_agents : index.display_workflows;
    const auto &other_canonical =
        kind == ExecutableKind::Agent ? index.canonical_workflows : index.canonical_agents;
    const auto &other_display =
        kind == ExecutableKind::Agent ? index.display_workflows : index.display_agents;

    if (const auto found = canonical.find(std::string(raw_name)); found != canonical.end()) {
        return *found;
    }

    if (const auto found = display.find(std::string(raw_name)); found != display.end()) {
        if (found->second.size() == 1) {
            return found->second.front();
        }

        diagnostics.error("package authoring field '" + std::string(field_name) +
                          "' is ambiguous for " + executable_kind_name(kind) + " '" +
                          std::string(raw_name) + "'");
        return std::nullopt;
    }

    if (other_canonical.contains(std::string(raw_name)) || other_display.contains(std::string(raw_name))) {
        diagnostics.error("package authoring field '" + std::string(field_name) + "' refers to '" +
                          std::string(raw_name) + "' with wrong executable kind");
        return std::nullopt;
    }

    diagnostics.error("unknown package authoring " + executable_kind_name(kind) + " target '" +
                      std::string(raw_name) + "' in field '" + std::string(field_name) + "'");
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
resolve_capability_name(const CapabilityIndex &index,
                        std::string_view raw_name,
                        DiagnosticBag &diagnostics) {
    if (const auto found = index.canonical.find(std::string(raw_name)); found != index.canonical.end()) {
        return *found;
    }

    if (const auto found = index.display.find(std::string(raw_name)); found != index.display.end()) {
        if (found->second.size() == 1) {
            return found->second.front();
        }

        diagnostics.error("package authoring capability binding is ambiguous for capability '" +
                          std::string(raw_name) + "'");
        return std::nullopt;
    }

    diagnostics.error("unknown package authoring capability '" + std::string(raw_name) + "'");
    return std::nullopt;
}

struct FormalObservationScopeKey {
    ir::FormalObservationScopeKind kind{ir::FormalObservationScopeKind::ContractClause};
    std::string owner;
    std::size_t clause_index{0};

    [[nodiscard]] friend bool operator==(const FormalObservationScopeKey &lhs,
                                         const FormalObservationScopeKey &rhs) noexcept = default;
};

struct FormalObservationScopeKeyHash {
    [[nodiscard]] std::size_t operator()(const FormalObservationScopeKey &value) const noexcept {
        const auto kind_hash = std::hash<int>{}(static_cast<int>(value.kind));
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

void push_unique_binding_reference(std::vector<CapabilityBindingReference> &values,
                                   const CapabilityBindingReference &value) {
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
        .required_by_targets = {},
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

WorkflowNodeLifecycleSummary lower_workflow_node_lifecycle(const ir::WorkflowNode &node,
                                                           const ir::AgentDecl *target_agent) {
    WorkflowNodeLifecycleSummary lifecycle{
        .start_condition = node.after.empty()
                               ? WorkflowNodeStartConditionKind::Immediate
                               : WorkflowNodeStartConditionKind::AfterDependenciesCompleted,
        .completion_condition = WorkflowNodeCompletionConditionKind::TargetReachedFinalState,
        .completion_latched = true,
        .target_initial_state = {},
        .target_final_states = {},
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

[[nodiscard]] std::unordered_map<FormalObservationScopeKey,
                                 std::vector<std::string>,
                                 FormalObservationScopeKeyHash>
index_formal_observation_symbols(const std::vector<ir::FormalObservation> &observations) {
    std::unordered_map<FormalObservationScopeKey,
                       std::vector<std::string>,
                       FormalObservationScopeKeyHash>
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
        .nodes = {},
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

PackageMetadataValidationResult validate_package_metadata(const ir::Program &program,
                                                         PackageMetadata metadata) {
    PackageMetadataValidationResult result{
        .metadata = std::move(metadata),
        .diagnostics = {},
    };

    const auto executable_index = build_executable_index(program);
    const auto capability_index = build_capability_index(program);

    if (result.metadata.entry_target.has_value()) {
        auto canonical_name = resolve_executable_name(executable_index,
                                                      result.metadata.entry_target->kind,
                                                      result.metadata.entry_target->canonical_name,
                                                      result.diagnostics,
                                                      "entry_target");
        if (canonical_name.has_value()) {
            result.metadata.entry_target->canonical_name = std::move(*canonical_name);
        }
    }

    std::unordered_set<std::string> seen_export_targets;
    for (auto &target : result.metadata.export_targets) {
        auto canonical_name = resolve_executable_name(executable_index,
                                                      target.kind,
                                                      target.canonical_name,
                                                      result.diagnostics,
                                                      "export_targets");
        if (!canonical_name.has_value()) {
            continue;
        }

        target.canonical_name = std::move(*canonical_name);
        const auto key =
            std::to_string(static_cast<int>(target.kind)) + ":" + target.canonical_name;
        if (!seen_export_targets.insert(key).second) {
            result.diagnostics.error("package authoring export target '" + target.canonical_name +
                                     "' is duplicated after semantic normalization");
        }
    }

    std::unordered_map<std::string, std::string> normalized_bindings;
    for (const auto &[raw_capability_name, binding_key] : result.metadata.capability_binding_keys) {
        auto canonical_name =
            resolve_capability_name(capability_index, raw_capability_name, result.diagnostics);
        if (!canonical_name.has_value()) {
            continue;
        }

        if (const auto found = normalized_bindings.find(*canonical_name);
            found != normalized_bindings.end()) {
            result.diagnostics.error("package authoring capability binding for '" + *canonical_name +
                                     "' is duplicated after semantic normalization");
            continue;
        }

        normalized_bindings.emplace(std::move(*canonical_name), binding_key);
    }
    result.metadata.capability_binding_keys = std::move(normalized_bindings);

    return result;
}

Package lower_package(const ir::Program &program, PackageMetadata metadata) {
    if (metadata.identity.has_value()) {
        metadata.identity->format_version = std::string(kFormatVersion);
    }

    Package package{
        .metadata = std::move(metadata),
        .executable_targets = {},
        .capability_binding_slots = {},
        .policy_obligations = {},
        .formal_observations = program.formal_observations,
    };

    std::unordered_map<std::string, std::vector<std::string>> agent_capabilities;
    std::unordered_map<std::string, const ir::AgentDecl *> agents_by_name;
    const auto formal_observation_index =
        index_formal_observation_symbols(program.formal_observations);
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

[[nodiscard]] std::vector<CapabilityBindingReference>
build_node_capability_binding_refs(const Package &package, std::string_view agent_canonical_name) {
    std::vector<CapabilityBindingReference> references;

    for (const auto &slot : package.capability_binding_slots) {
        for (const auto &target : slot.required_by_targets) {
            if (target.kind == ExecutableKind::Agent &&
                target.canonical_name == agent_canonical_name) {
                push_unique_binding_reference(
                    references,
                    CapabilityBindingReference{
                        .capability_name = slot.capability_name,
                        .binding_key = slot.binding_key,
                    });
                break;
            }
        }
    }

    return references;
}

[[nodiscard]] bool contains_string(const std::vector<std::string> &values,
                                   std::string_view needle) {
    for (const auto &value : values) {
        if (value == needle) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::string edge_key(std::string_view from_node, std::string_view to_node) {
    std::string key;
    key.reserve(from_node.size() + to_node.size() + 1U);
    key.append(from_node);
    key.push_back('\0');
    key.append(to_node);
    return key;
}

void validate_workflow_value_summary(const ir::WorkflowExprSummary &summary,
                                     const std::unordered_set<std::string> &node_names,
                                     std::string_view owner,
                                     DiagnosticBag &diagnostics) {
    for (const auto &read : summary.reads) {
        switch (read.kind) {
        case ir::WorkflowValueSourceKind::WorkflowInput:
            if (read.root_name != "input") {
                diagnostics.error("execution plan validation " + std::string(owner) +
                                  " reads workflow input root '" + read.root_name +
                                  "' instead of 'input'");
            }
            break;
        case ir::WorkflowValueSourceKind::WorkflowNodeOutput:
            if (!node_names.contains(read.root_name)) {
                diagnostics.error("execution plan validation " + std::string(owner) +
                                  " reads unknown workflow node output '" + read.root_name + "'");
            }
            break;
        }
    }
}

void validate_workflow_plan(const WorkflowPlan &workflow, DiagnosticBag &diagnostics) {
    if (workflow.workflow_canonical_name.empty()) {
        diagnostics.error("execution plan validation contains workflow with empty canonical name");
    }

    const auto workflow_name = workflow.workflow_canonical_name.empty()
                                   ? std::string("<unknown>")
                                   : workflow.workflow_canonical_name;

    std::unordered_set<std::string> node_names;
    std::unordered_map<std::string, const WorkflowNodePlan *> nodes_by_name;
    for (const auto &node : workflow.nodes) {
        if (node.name.empty()) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' contains node with empty name");
            continue;
        }

        if (!node_names.insert(node.name).second) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' contains duplicate node '" + node.name + "'");
            continue;
        }

        nodes_by_name.emplace(node.name, &node);
    }

    if (workflow.nodes.empty()) {
        diagnostics.error("execution plan validation workflow '" + workflow_name +
                          "' contains no nodes");
    }

    std::unordered_set<std::string> entry_nodes;
    for (const auto &entry_node : workflow.entry_nodes) {
        if (!entry_nodes.insert(entry_node).second) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' contains duplicate entry node '" + entry_node + "'");
        }

        const auto found = nodes_by_name.find(entry_node);
        if (found == nodes_by_name.end()) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' entry node '" + entry_node + "' does not exist");
            continue;
        }

        if (!found->second->after.empty()) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' entry node '" + entry_node + "' has dependencies");
        }
    }

    std::unordered_set<std::string> edge_keys;
    std::unordered_map<std::string, std::vector<std::string>> outgoing_edges;
    std::unordered_map<std::string, std::size_t> incoming_edge_count;
    for (const auto &node_name : node_names) {
        incoming_edge_count.emplace(node_name, 0U);
    }

    for (const auto &edge : workflow.dependency_edges) {
        if (!node_names.contains(edge.from_node) || !node_names.contains(edge.to_node)) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' dependency edge '" + edge.from_node + " -> " + edge.to_node +
                              "' refers to unknown node");
            continue;
        }

        const auto key = edge_key(edge.from_node, edge.to_node);
        if (!edge_keys.insert(key).second) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' contains duplicate dependency edge '" + edge.from_node +
                              " -> " + edge.to_node + "'");
        }

        outgoing_edges[edge.from_node].push_back(edge.to_node);
        incoming_edge_count[edge.to_node] += 1U;

        const auto found_target = nodes_by_name.find(edge.to_node);
        if (found_target != nodes_by_name.end() &&
            !contains_string(found_target->second->after, edge.from_node)) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' dependency edge '" + edge.from_node + " -> " + edge.to_node +
                              "' is not mirrored by node '" + edge.to_node + "' after list");
        }
    }

    for (const auto &node : workflow.nodes) {
        if (node.target.empty()) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' node '" + node.name + "' has empty target");
        }

        const auto expected_start_condition =
            node.after.empty() ? WorkflowNodeStartConditionKind::Immediate
                               : WorkflowNodeStartConditionKind::AfterDependenciesCompleted;
        if (node.lifecycle.start_condition != expected_start_condition) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' node '" + node.name +
                              "' start condition does not match dependencies");
        }

        if (node.lifecycle.target_final_states.empty()) {
            diagnostics.error("execution plan validation workflow '" + workflow_name +
                              "' node '" + node.name +
                              "' lifecycle has no target final state");
        }
        for (const auto &state : node.lifecycle.target_final_states) {
            if (state.empty()) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name +
                                  "' lifecycle contains empty target final state");
            }
        }

        std::unordered_set<std::string> seen_dependencies;
        for (const auto &dependency : node.after) {
            if (!seen_dependencies.insert(dependency).second) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name +
                                  "' contains duplicate after dependency '" + dependency + "'");
            }

            if (!node_names.contains(dependency)) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name + "' depends on unknown node '" +
                                  dependency + "'");
                continue;
            }

            if (!edge_keys.contains(edge_key(dependency, node.name))) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name + "' after dependency '" + dependency +
                                  "' is missing dependency edge");
            }
        }

        std::unordered_set<std::string> binding_refs;
        for (const auto &binding : node.capability_bindings) {
            if (binding.capability_name.empty()) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name +
                                  "' has capability binding with empty capability name");
            }
            if (binding.binding_key.empty()) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name +
                                  "' has capability binding with empty binding key");
            }

            const auto binding_key = edge_key(binding.capability_name, binding.binding_key);
            if (!binding_refs.insert(binding_key).second) {
                diagnostics.error("execution plan validation workflow '" + workflow_name +
                                  "' node '" + node.name +
                                  "' contains duplicate capability binding '" +
                                  binding.capability_name + "'");
            }
        }

        validate_workflow_value_summary(node.input_summary,
                                        node_names,
                                        "workflow '" + workflow_name + "' node '" + node.name +
                                            "' input_summary",
                                        diagnostics);
    }

    std::vector<std::string> ready_nodes;
    ready_nodes.reserve(node_names.size());
    for (const auto &[node_name, incoming_count] : incoming_edge_count) {
        if (incoming_count == 0U) {
            ready_nodes.push_back(node_name);
        }
    }

    std::size_t visited_node_count = 0;
    for (std::size_t index = 0; index < ready_nodes.size(); ++index) {
        const auto node_name = ready_nodes[index];
        ++visited_node_count;

        const auto found_edges = outgoing_edges.find(node_name);
        if (found_edges == outgoing_edges.end()) {
            continue;
        }

        for (const auto &to_node : found_edges->second) {
            auto &incoming_count = incoming_edge_count[to_node];
            if (incoming_count == 0U) {
                continue;
            }
            incoming_count -= 1U;
            if (incoming_count == 0U) {
                ready_nodes.push_back(to_node);
            }
        }
    }

    if (visited_node_count != node_names.size()) {
        diagnostics.error("execution plan validation workflow '" + workflow_name +
                          "' dependency graph contains a cycle");
    }

    validate_workflow_value_summary(workflow.return_summary,
                                    node_names,
                                    "workflow '" + workflow_name + "' return_summary",
                                    diagnostics);
}

PackageReaderSummaryResult build_package_reader_summary(const Package &package) {
    PackageReaderSummaryResult result{
        .summary =
            PackageReaderSummary{
                .format_version = std::string(kFormatVersion),
                .identity = package.metadata.identity,
                .entry_target = package.metadata.entry_target,
                .export_targets = package.metadata.export_targets,
                .capability_binding_slots = package.capability_binding_slots,
                .policy_obligations = package.policy_obligations,
                .formal_observations = {},
            },
        .diagnostics = {},
    };

    std::unordered_set<std::string> agent_targets;
    std::unordered_set<std::string> workflow_targets;
    for (const auto &target : package.executable_targets) {
        if (const auto *agent = std::get_if<AgentExecutable>(&target); agent != nullptr) {
            agent_targets.insert(agent->canonical_name);
            continue;
        }

        if (const auto *workflow = std::get_if<WorkflowExecutable>(&target); workflow != nullptr) {
            workflow_targets.insert(workflow->canonical_name);
        }
    }

    auto has_executable_target = [&](const ExecutableRef &target) {
        return target.kind == ExecutableKind::Agent
                   ? agent_targets.contains(target.canonical_name)
                   : workflow_targets.contains(target.canonical_name);
    };

    if (result.summary.identity.has_value() &&
        result.summary.identity->format_version != kFormatVersion) {
        result.diagnostics.error("package reader summary encountered unsupported format_version '" +
                                 result.summary.identity->format_version + "'");
    }

    if (result.summary.entry_target.has_value() &&
        !has_executable_target(*result.summary.entry_target)) {
        result.diagnostics.error("package reader summary entry target '" +
                                 result.summary.entry_target->canonical_name +
                                 "' does not exist in executable targets");
    }

    for (const auto &target : result.summary.export_targets) {
        if (!has_executable_target(target)) {
            result.diagnostics.error("package reader summary export target '" +
                                     target.canonical_name +
                                     "' does not exist in executable targets");
        }
    }

    std::unordered_set<std::string> binding_keys;
    for (const auto &slot : result.summary.capability_binding_slots) {
        if (!binding_keys.insert(slot.binding_key).second) {
            result.diagnostics.error("package reader summary contains duplicate capability binding key '" +
                                     slot.binding_key + "'");
        }

        for (const auto &target : slot.required_by_targets) {
            if (!has_executable_target(target)) {
                result.diagnostics.error("package reader summary capability binding target '" +
                                         target.canonical_name +
                                         "' does not exist in executable targets");
            }
        }
    }

    for (const auto &obligation : result.summary.policy_obligations) {
        if (!has_executable_target(obligation.owner_target)) {
            result.diagnostics.error("package reader summary policy obligation owner '" +
                                     obligation.owner_target.canonical_name +
                                     "' does not exist in executable targets");
        }
    }

    result.summary.formal_observations.total = package.formal_observations.size();
    for (const auto &observation : package.formal_observations) {
        std::visit(Overloaded{
                       [&](const ir::CalledCapabilityObservation &) {
                           result.summary.formal_observations.called_capability += 1;
                       },
                       [&](const ir::EmbeddedBoolObservation &) {
                           result.summary.formal_observations.embedded_bool_expr += 1;
                       },
                   },
                   observation.node);
    }

    return result;
}

ExecutionPlannerBootstrapResult
build_execution_planner_bootstrap(const Package &package, std::string_view workflow_canonical_name) {
    ExecutionPlannerBootstrapResult result{
        .bootstrap = std::nullopt,
        .diagnostics = {},
    };

    const auto *workflow = find_workflow_executable(package, workflow_canonical_name);
    if (workflow == nullptr) {
        if (find_agent_executable(package, workflow_canonical_name) != nullptr) {
            result.diagnostics.error("execution planner bootstrap target '" +
                                     std::string(workflow_canonical_name) +
                                     "' is not a workflow target");
            return result;
        }

        result.diagnostics.error("unknown execution planner workflow target '" +
                                 std::string(workflow_canonical_name) + "'");
        return result;
    }

    ExecutionPlannerBootstrap bootstrap{
        .workflow_canonical_name = workflow->canonical_name,
        .input_type = workflow->input_type,
        .output_type = workflow->output_type,
        .entry_nodes = workflow->execution_graph.entry_nodes,
        .dependency_edges = workflow->execution_graph.dependency_edges,
        .nodes = {},
        .return_summary = workflow->return_summary,
    };
    bootstrap.nodes.reserve(workflow->nodes.size());
    for (const auto &node : workflow->nodes) {
        bootstrap.nodes.push_back(ExecutionPlannerBootstrapNode{
            .name = node.name,
            .target = node.target,
            .after = node.after,
            .lifecycle = node.lifecycle,
            .input_summary = node.input_summary,
        });
    }

    std::unordered_set<std::string> node_names;
    for (const auto &node : bootstrap.nodes) {
        node_names.insert(node.name);
        if (find_agent_executable(package, node.target) == nullptr) {
            result.diagnostics.error("execution planner bootstrap node '" + node.name +
                                     "' targets unknown agent '" + node.target + "'");
        }
    }

    for (const auto &entry_node : bootstrap.entry_nodes) {
        if (!node_names.contains(entry_node)) {
            result.diagnostics.error("execution planner bootstrap entry node '" + entry_node +
                                     "' does not exist in workflow '" +
                                     bootstrap.workflow_canonical_name + "'");
        }
    }

    for (const auto &edge : bootstrap.dependency_edges) {
        if (!node_names.contains(edge.from_node) || !node_names.contains(edge.to_node)) {
            result.diagnostics.error("execution planner bootstrap dependency '" + edge.from_node +
                                     " -> " + edge.to_node +
                                     "' refers to unknown workflow node");
        }
    }

    for (const auto &node : bootstrap.nodes) {
        for (const auto &dependency : node.after) {
            if (!node_names.contains(dependency)) {
                result.diagnostics.error("execution planner bootstrap node '" + node.name +
                                         "' depends on unknown node '" + dependency + "'");
            }
        }
    }

    result.bootstrap = std::move(bootstrap);
    return result;
}

ExecutionPlannerBootstrapResult build_entry_execution_planner_bootstrap(const Package &package) {
    if (!package.metadata.entry_target.has_value()) {
        return ExecutionPlannerBootstrapResult{
            .bootstrap = std::nullopt,
            .diagnostics =
                [&]() {
                    DiagnosticBag diagnostics;
                    diagnostics.error(
                        "package does not define a workflow entry target for execution planner bootstrap");
                    return diagnostics;
                }(),
        };
    }

    if (package.metadata.entry_target->kind != ExecutableKind::Workflow) {
        return ExecutionPlannerBootstrapResult{
            .bootstrap = std::nullopt,
            .diagnostics =
                [&]() {
                    DiagnosticBag diagnostics;
                    diagnostics.error("package entry target '" +
                                      package.metadata.entry_target->canonical_name +
                                      "' is not a workflow target for execution planner bootstrap");
                    return diagnostics;
                }(),
        };
    }

    return build_execution_planner_bootstrap(package,
                                             package.metadata.entry_target->canonical_name);
}

ExecutionPlanValidationResult validate_execution_plan(const ExecutionPlan &plan) {
    ExecutionPlanValidationResult result{
        .diagnostics = {},
    };

    if (plan.format_version != kExecutionPlanFormatVersion) {
        result.diagnostics.error(
            "execution plan validation encountered unsupported format_version '" +
            plan.format_version + "'");
    }

    std::unordered_set<std::string> workflow_names;
    for (const auto &workflow : plan.workflows) {
        if (workflow.workflow_canonical_name.empty()) {
            continue;
        }

        if (!workflow_names.insert(workflow.workflow_canonical_name).second) {
            result.diagnostics.error("execution plan validation contains duplicate workflow '" +
                                     workflow.workflow_canonical_name + "'");
        }
    }

    if (plan.workflows.empty()) {
        result.diagnostics.error("execution plan validation contains no workflows");
    }

    if (plan.entry_workflow_canonical_name.has_value() &&
        !workflow_names.contains(*plan.entry_workflow_canonical_name)) {
        result.diagnostics.error("execution plan validation entry workflow '" +
                                 *plan.entry_workflow_canonical_name +
                                 "' does not exist in workflows");
    }

    for (const auto &workflow : plan.workflows) {
        validate_workflow_plan(workflow, result.diagnostics);
    }

    return result;
}

ExecutionPlanResult build_execution_plan(const Package &package) {
    ExecutionPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    ExecutionPlan plan{
        .format_version = std::string(kExecutionPlanFormatVersion),
        .source_package_identity = package.metadata.identity,
        .entry_workflow_canonical_name = std::nullopt,
        .workflows = {},
    };

    const auto reader_summary = build_package_reader_summary(package);
    result.diagnostics.append(reader_summary.diagnostics);

    if (package.metadata.entry_target.has_value()) {
        const auto entry_bootstrap = build_entry_execution_planner_bootstrap(package);
        result.diagnostics.append(entry_bootstrap.diagnostics);
        if (entry_bootstrap.bootstrap.has_value()) {
            plan.entry_workflow_canonical_name =
                entry_bootstrap.bootstrap->workflow_canonical_name;
        }
    }

    for (const auto &target : package.executable_targets) {
        const auto *workflow = std::get_if<WorkflowExecutable>(&target);
        if (workflow == nullptr) {
            continue;
        }

        const auto bootstrap =
            build_execution_planner_bootstrap(package, workflow->canonical_name);
        result.diagnostics.append(bootstrap.diagnostics);
        if (!bootstrap.bootstrap.has_value()) {
            continue;
        }

        WorkflowPlan workflow_plan{
            .workflow_canonical_name = bootstrap.bootstrap->workflow_canonical_name,
            .input_type = bootstrap.bootstrap->input_type,
            .output_type = bootstrap.bootstrap->output_type,
            .entry_nodes = bootstrap.bootstrap->entry_nodes,
            .dependency_edges = bootstrap.bootstrap->dependency_edges,
            .nodes = {},
            .return_summary = bootstrap.bootstrap->return_summary,
        };
        workflow_plan.nodes.reserve(bootstrap.bootstrap->nodes.size());
        for (const auto &node : bootstrap.bootstrap->nodes) {
            workflow_plan.nodes.push_back(WorkflowNodePlan{
                .name = node.name,
                .target = node.target,
                .after = node.after,
                .lifecycle = node.lifecycle,
                .input_summary = node.input_summary,
                .capability_bindings =
                    build_node_capability_binding_refs(package, node.target),
            });
        }

        plan.workflows.push_back(std::move(workflow_plan));
    }

    const auto plan_validation = validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);

    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(plan);
    return result;
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

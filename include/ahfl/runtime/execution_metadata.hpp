#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"

namespace ahfl::runtime {

struct WorkflowMetadata {
    std::string display_name;
};

struct AgentMetadata {
    std::string display_name;
};

struct WorkflowNodeMetadata {
    std::string display_name;
    WorkflowId workflow;
    AgentId agent;
};

struct CapabilityMetadata {
    std::string display_name;
    std::optional<std::size_t> source_symbol_id;
};

struct ProviderMetadata {
    std::string display_name;
};

struct AgentStateMetadata {
    AgentId agent;
    std::string display_name;
};

struct InvocationMetadata {
    WorkflowNodeId node;
    CapabilityId capability;
};

class ExecutionMetadataStore {
  public:
    [[nodiscard]] WorkflowId add_workflow(std::string_view display_name);
    [[nodiscard]] AgentId add_agent(std::string_view display_name);
    [[nodiscard]] WorkflowNodeId
    add_node(std::string_view display_name, WorkflowId workflow, AgentId agent);
    [[nodiscard]] CapabilityId
    add_capability(std::string_view display_name,
                   std::optional<std::size_t> source_symbol_id = std::nullopt);
    [[nodiscard]] ProviderId add_provider(std::string_view display_name);
    [[nodiscard]] AgentStateId add_agent_state(AgentId agent, std::string_view display_name);
    [[nodiscard]] InvocationId add_invocation(WorkflowNodeId node, CapabilityId capability);

    [[nodiscard]] const WorkflowMetadata *workflow(WorkflowId id) const noexcept;
    [[nodiscard]] const AgentMetadata *agent(AgentId id) const noexcept;
    [[nodiscard]] const WorkflowNodeMetadata *node(WorkflowNodeId id) const noexcept;
    [[nodiscard]] const CapabilityMetadata *capability(CapabilityId id) const noexcept;
    [[nodiscard]] std::optional<CapabilityId>
    capability_for_source_symbol(std::size_t source_symbol_id) const noexcept;
    [[nodiscard]] const ProviderMetadata *provider(ProviderId id) const noexcept;
    [[nodiscard]] const AgentStateMetadata *agent_state(AgentStateId id) const noexcept;
    [[nodiscard]] const InvocationMetadata *invocation(InvocationId id) const noexcept;

  private:
    std::vector<WorkflowMetadata> workflows_;
    std::vector<AgentMetadata> agents_;
    std::vector<WorkflowNodeMetadata> nodes_;
    std::vector<CapabilityMetadata> capabilities_;
    std::vector<ProviderMetadata> providers_;
    std::vector<AgentStateMetadata> agent_states_;
    std::vector<InvocationMetadata> invocations_;
};

} // namespace ahfl::runtime

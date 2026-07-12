#include "ahfl/runtime/execution_metadata.hpp"

namespace ahfl::runtime {
namespace {

template <typename Id, typename Entry>
[[nodiscard]] const Entry *find_entry(const std::vector<Entry> &entries, Id id) noexcept {
    if (!id.valid() || id.index() >= entries.size()) {
        return nullptr;
    }
    return &entries[id.index()];
}

} // namespace

WorkflowId ExecutionMetadataStore::add_workflow(std::string_view display_name) {
    const WorkflowId id{workflows_.size()};
    workflows_.push_back(WorkflowMetadata{.display_name = std::string(display_name)});
    return id;
}

AgentId ExecutionMetadataStore::add_agent(std::string_view display_name) {
    const AgentId id{agents_.size()};
    agents_.push_back(AgentMetadata{.display_name = std::string(display_name)});
    return id;
}

WorkflowNodeId ExecutionMetadataStore::add_node(std::string_view display_name,
                                                WorkflowId workflow,
                                                AgentId agent) {
    const WorkflowNodeId id{nodes_.size()};
    nodes_.push_back(WorkflowNodeMetadata{
        .display_name = std::string(display_name),
        .workflow = workflow,
        .agent = agent,
    });
    return id;
}

CapabilityId
ExecutionMetadataStore::add_capability(std::string_view display_name,
                                       std::optional<std::size_t> source_symbol_id) {
    const CapabilityId id{capabilities_.size()};
    capabilities_.push_back(CapabilityMetadata{
        .display_name = std::string(display_name),
        .source_symbol_id = source_symbol_id,
    });
    return id;
}

ProviderId ExecutionMetadataStore::add_provider(std::string_view display_name) {
    const ProviderId id{providers_.size()};
    providers_.push_back(ProviderMetadata{.display_name = std::string(display_name)});
    return id;
}

AgentStateId ExecutionMetadataStore::add_agent_state(AgentId agent,
                                                     std::string_view display_name) {
    const AgentStateId id{agent_states_.size()};
    agent_states_.push_back(AgentStateMetadata{
        .agent = agent,
        .display_name = std::string(display_name),
    });
    return id;
}

InvocationId ExecutionMetadataStore::add_invocation(WorkflowNodeId node,
                                                    CapabilityId capability) {
    const InvocationId id{invocations_.size()};
    invocations_.push_back(InvocationMetadata{.node = node, .capability = capability});
    return id;
}

const WorkflowMetadata *ExecutionMetadataStore::workflow(WorkflowId id) const noexcept {
    return find_entry(workflows_, id);
}

const AgentMetadata *ExecutionMetadataStore::agent(AgentId id) const noexcept {
    return find_entry(agents_, id);
}

const WorkflowNodeMetadata *ExecutionMetadataStore::node(WorkflowNodeId id) const noexcept {
    return find_entry(nodes_, id);
}

const CapabilityMetadata *
ExecutionMetadataStore::capability(CapabilityId id) const noexcept {
    return find_entry(capabilities_, id);
}

std::optional<CapabilityId>
ExecutionMetadataStore::capability_for_source_symbol(std::size_t source_symbol_id) const noexcept {
    for (std::size_t index = 0; index < capabilities_.size(); ++index) {
        if (capabilities_[index].source_symbol_id == source_symbol_id) {
            return CapabilityId{index};
        }
    }
    return std::nullopt;
}

const ProviderMetadata *ExecutionMetadataStore::provider(ProviderId id) const noexcept {
    return find_entry(providers_, id);
}

const AgentStateMetadata *
ExecutionMetadataStore::agent_state(AgentStateId id) const noexcept {
    return find_entry(agent_states_, id);
}

const InvocationMetadata *
ExecutionMetadataStore::invocation(InvocationId id) const noexcept {
    return find_entry(invocations_, id);
}

} // namespace ahfl::runtime

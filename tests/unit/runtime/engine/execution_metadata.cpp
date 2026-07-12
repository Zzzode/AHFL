#include "ahfl/runtime/execution_metadata.hpp"

#include <cstdio>
#include <type_traits>

namespace {

using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

void test_metadata_uses_flat_typed_stores() {
    ExecutionMetadataStore metadata;

    const auto workflow = metadata.add_workflow("app::MainWorkflow");
    const auto agent = metadata.add_agent("app::Worker");
    const auto node = metadata.add_node("work", workflow, agent);
    const auto capability = metadata.add_capability("app::Fetch");
    const auto provider = metadata.add_provider("primary");
    const auto state = metadata.add_agent_state(agent, "Done");

    static_assert(!std::is_same_v<WorkflowId, WorkflowNodeId>);
    static_assert(!std::is_same_v<AgentId, AgentStateId>);
    check(workflow == WorkflowId{0}, "workflow id indexes workflow store");
    check(agent == AgentId{0}, "agent id indexes agent store");
    check(node == WorkflowNodeId{0}, "node id indexes node store");
    check(capability == CapabilityId{0}, "capability id indexes capability store");
    check(provider == ProviderId{0}, "provider id indexes provider store");
    check(state == AgentStateId{0}, "state id indexes state store");

    check(metadata.workflow(workflow) != nullptr, "workflow lookup succeeds");
    check(metadata.workflow(workflow)->display_name == "app::MainWorkflow",
          "workflow display name materializes at boundary");
    check(metadata.node(node)->workflow == workflow, "node references workflow by id");
    check(metadata.node(node)->agent == agent, "node references agent by id");
    check(metadata.agent_state(state)->agent == agent, "state references agent by id");
    check(metadata.node(WorkflowNodeId{1}) == nullptr, "out of bounds node id is rejected");
}

} // namespace

int main() {
    test_metadata_uses_flat_typed_stores();
    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

#include "ahfl/runtime/execution_report.hpp"

#include <chrono>
#include <cstdio>

namespace {

using namespace ahfl::runtime;
using namespace std::chrono_literals;

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

template <typename Payload>
void append_event(ExecutionEventStore &store,
                  Payload payload,
                  std::chrono::nanoseconds monotonic_offset) {
    (void)store.append(std::move(payload), monotonic_offset);
}

void test_report_is_aggregated_from_events() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId first{0};
    const WorkflowNodeId second{1};
    const AgentId first_agent{0};
    const AgentId second_agent{1};
    const RuntimeValueId node_value{0};
    const RuntimeValueId final_value{1};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 1ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = first,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(store, NodeStarted{.node = first, .agent = first_agent}, 3ns);
    append_event(store, NodeCompleted{.node = first, .output = node_value}, 4ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = second,
                     .dependencies = {first},
                     .execution_slot = 1,
                 },
                 5ns);
    append_event(store, NodeStarted{.node = second, .agent = second_agent}, 6ns);
    append_event(store,
                 NodeFailed{
                     .node = second,
                     .diagnostic = DiagnosticId{0},
                     .kind = NodeFailureKind::AgentFailed,
                 },
                 7ns);
    append_event(store,
                 WorkflowFailed{.workflow = workflow, .diagnostic = DiagnosticId{0}},
                 8ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Failed}, 9ns);

    const auto result = build_execution_report(store.events());
    check(result.has_value(), "valid events build a report");
    if (!result.has_value()) {
        return;
    }
    check(result->run == run, "report retains run id");
    check(result->workflow == workflow, "report retains workflow id");
    check(result->status == RunTerminalStatus::Failed, "report status comes from terminal event");
    check(!result->output.has_value(), "failed workflow has no final output");
    check(result->execution_order.size() == 2, "execution order derives from scheduling events");
    check(result->execution_order[0] == first, "first scheduled node is first");
    check(result->execution_order[1] == second, "second scheduled node is second");
    check(result->nodes.size() == 2, "report contains two node projections");
    check(result->nodes[0].node == first, "first node report keeps id");
    check(result->nodes[0].agent == first_agent, "first node report keeps agent id");
    check(result->nodes[0].status == NodeReportStatus::Completed,
          "node completed event determines report status");
    check(result->nodes[0].output == std::optional{node_value},
          "node output is an ID reference");
    check(result->nodes[1].status == NodeReportStatus::Failed,
          "node failed event determines report status");
    check(!result->nodes[1].output.has_value(), "failed node has no output");

    (void)final_value;
}

void test_report_rejects_invalid_lifecycle() {
    ExecutionEventStore store;
    append_event(store, RunStarted{.run = RunId{0}}, 0ns);

    const auto result = build_execution_report(store.events());
    check(!result.has_value(), "report rejects event stream without terminal events");
}

void test_skipped_node_is_projected_without_agent_start() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId blocked{0};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 1ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = blocked,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(store,
                 NodeSkipped{.node = blocked, .blocking_dependencies = {}},
                 3ns);
    append_event(store,
                 WorkflowFailed{.workflow = workflow, .diagnostic = DiagnosticId{0}},
                 4ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Failed}, 5ns);

    const auto result = build_execution_report(store.events());
    check(result.has_value(), "skipped node lifecycle builds a report");
    if (result.has_value()) {
        check(result->nodes.size() == 1, "skipped node appears in report");
        check(result->nodes[0].status == NodeReportStatus::Skipped,
              "skipped event determines report status");
        check(!result->nodes[0].agent.valid(), "skipped node has no started agent");
    }
}

} // namespace

int main() {
    test_report_is_aggregated_from_events();
    test_report_rejects_invalid_lifecycle();
    test_skipped_node_is_projected_without_agent_start();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

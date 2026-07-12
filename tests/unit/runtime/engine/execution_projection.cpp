#include "ahfl/runtime/execution_projection.hpp"

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <chrono>
#include <cstdio>

namespace {

using namespace ahfl::runtime;
using namespace ahfl::evaluator;
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
void append_event(WorkflowResult &result,
                  Payload payload,
                  std::chrono::nanoseconds monotonic_offset) {
    (void)result.events.append(std::move(payload), monotonic_offset);
}

WorkflowResult make_projection_result() {
    WorkflowResult result;
    const auto workflow = result.metadata.add_workflow("demo::Workflow");
    const auto first_agent = result.metadata.add_agent("demo::FirstAgent");
    const auto second_agent = result.metadata.add_agent("demo::SecondAgent");
    const auto first = result.metadata.add_node("first", workflow, first_agent);
    const auto second = result.metadata.add_node("second", workflow, second_agent);
    result.values.push_back(make_string("done"));
    const RuntimeValueId value{0};

    append_event(result, RunStarted{.run = RunId{0}}, 0ns);
    append_event(result, WorkflowStarted{.run = RunId{0}, .workflow = workflow}, 1ns);
    append_event(result,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = first,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(result,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = second,
                     .dependencies = {first},
                     .execution_slot = 1,
                 },
                 3ns);
    append_event(result, NodeStarted{.node = first, .agent = first_agent}, 4ns);
    append_event(result, NodeCompleted{.node = first, .output = value}, 5ns);
    append_event(result,
                 NodeSkipped{.node = second, .blocking_dependencies = {first}},
                 6ns);
    append_event(result,
                 WorkflowFailed{
                     .workflow = workflow,
                     .diagnostic = DiagnosticId{0},
                     .kind = WorkflowFailureKind::DependencyFailed,
                 },
                 7ns);
    append_event(result, CheckpointSaved{.run = RunId{0}, .checkpoint = CheckpointId{0}}, 8ns);
    append_event(result, RunCompleted{.run = RunId{0}, .status = RunTerminalStatus::Failed}, 9ns);
    const auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = *report;
    }
    return result;
}

void test_replay_projection_derives_node_progression_from_events() {
    const auto result = make_projection_result();
    const auto replay = build_execution_replay_projection(result);

    check(replay.has_value(), "replay projection builds");
    if (!replay.has_value()) {
        return;
    }
    check(replay->run == RunId{0}, "replay run id");
    check(replay->workflow == WorkflowId{0}, "replay workflow id");
    check(replay->status == RunTerminalStatus::Failed, "replay status");
    check(replay->execution_order.size() == 2, "replay execution order");
    check(replay->nodes.size() == 2, "replay node count");
    check(replay->nodes[0].node == WorkflowNodeId{0}, "replay first node id");
    check(replay->nodes[0].scheduled, "replay first scheduled");
    check(replay->nodes[0].started, "replay first started");
    check(replay->nodes[0].terminal == ReplayNodeTerminal::Completed,
          "replay first completed");
    check(replay->nodes[1].terminal == ReplayNodeTerminal::Skipped,
          "replay second skipped");
    check(replay->nodes[1].blocking_dependencies == std::vector{WorkflowNodeId{0}},
          "replay blocking dependency IDs");
    check(replay->checkpoints == std::vector{CheckpointId{0}}, "replay checkpoint IDs");
}

void test_audit_projection_counts_the_same_event_source() {
    const auto result = make_projection_result();
    const auto audit = build_execution_audit_projection(result);

    check(audit.has_value(), "audit projection builds");
    if (!audit.has_value()) {
        return;
    }
    check(audit->run == RunId{0}, "audit run id");
    check(audit->total_events == result.events.size(), "audit total event count");
    check(audit->node_scheduled == 2, "audit scheduled count");
    check(audit->node_started == 1, "audit started count");
    check(audit->node_completed == 1, "audit completed count");
    check(audit->node_skipped == 1, "audit skipped count");
    check(audit->workflow_failed == 1, "audit workflow failed count");
    check(audit->checkpoints_saved == 1, "audit checkpoint count");
    check(audit->terminal_invariant_holds, "audit terminal invariant");
}

void test_scheduler_projection_derives_terminal_node_states_from_events() {
    const auto result = make_projection_result();
    const auto scheduler = build_execution_scheduler_projection(result);

    check(scheduler.has_value(), "scheduler projection builds");
    if (!scheduler.has_value()) {
        return;
    }
    check(scheduler->run == RunId{0}, "scheduler run id");
    check(scheduler->workflow == WorkflowId{0}, "scheduler workflow id");
    check(scheduler->status == ExecutionSchedulerStatus::TerminalFailed,
          "scheduler terminal failed");
    check(scheduler->nodes.size() == 2, "scheduler node count");
    check(scheduler->nodes[0].state == ExecutionSchedulerNodeState::Completed,
          "scheduler first completed");
    check(scheduler->nodes[0].satisfied_dependencies.empty(),
          "scheduler first has no dependencies");
    check(scheduler->nodes[1].state == ExecutionSchedulerNodeState::Skipped,
          "scheduler second skipped");
    check(scheduler->nodes[1].satisfied_dependencies == std::vector{WorkflowNodeId{0}},
          "scheduler dependency completion derived from events");
    check(scheduler->nodes[1].blocking_dependencies == std::vector{WorkflowNodeId{0}},
          "scheduler skip blockers retain numeric IDs");
    check(scheduler->completed_prefix == std::vector{WorkflowNodeId{0}},
          "scheduler completed prefix");
    check(scheduler->next_candidate == WorkflowNodeId{1},
          "scheduler resume candidate is first non-completed node");
}

void test_checkpoint_projection_materializes_recovery_snapshot_from_event_values() {
    const auto result = make_projection_result();
    const auto checkpoint = build_execution_checkpoint_projection(result);

    check(checkpoint.has_value(), "checkpoint projection builds");
    if (!checkpoint.has_value()) {
        return;
    }
    check(checkpoint->checkpoint == CheckpointId{0}, "checkpoint latest id");
    check(checkpoint->completed_nodes.size() == 1, "checkpoint completed node count");
    check(checkpoint->completed_nodes[0].node == WorkflowNodeId{0},
          "checkpoint completed node id");
    check(checkpoint->completed_nodes[0].output == RuntimeValueId{0},
          "checkpoint output value id");
    check(checkpoint->resume_candidate == WorkflowNodeId{1},
          "checkpoint resume candidate");
    check(checkpoint->resume_ready, "checkpoint resume ready");

    const auto snapshot = materialize_workflow_recovery_snapshot(result, *checkpoint);
    check(snapshot.has_value(), "checkpoint materializes recovery snapshot");
    if (!snapshot.has_value()) {
        return;
    }
    check(snapshot->workflow == WorkflowId{0}, "snapshot workflow id");
    check(snapshot->checkpoint == CheckpointId{0}, "snapshot checkpoint id");
    check(snapshot->completed_nodes.size() == 1, "snapshot node count");
    check(snapshot->completed_nodes[0].agent == AgentId{0}, "snapshot agent id");
    check(snapshot->completed_nodes[0].output.has_value(), "snapshot output exists");
    if (snapshot->completed_nodes[0].output.has_value()) {
        check(value_to_json(*snapshot->completed_nodes[0].output) == "\"done\"",
              "snapshot clones event value");
    }
}

void test_projection_rejects_invalid_event_stream() {
    WorkflowResult result;
    append_event(result, RunStarted{.run = RunId{0}}, 0ns);

    check(!build_execution_replay_projection(result).has_value(),
          "replay rejects missing terminal");
    check(!build_execution_audit_projection(result).has_value(),
          "audit rejects missing terminal");
    check(!build_execution_scheduler_projection(result).has_value(),
          "scheduler rejects missing terminal");
    check(!build_execution_checkpoint_projection(result).has_value(),
          "checkpoint rejects missing terminal");
}

} // namespace

int main() {
    test_replay_projection_derives_node_progression_from_events();
    test_audit_projection_counts_the_same_event_source();
    test_scheduler_projection_derives_terminal_node_states_from_events();
    test_checkpoint_projection_materializes_recovery_snapshot_from_event_values();
    test_projection_rejects_invalid_event_stream();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

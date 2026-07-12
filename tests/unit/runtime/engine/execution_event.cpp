#include "ahfl/runtime/execution_event.hpp"

#include <chrono>
#include <cstdio>
#include <type_traits>
#include <variant>

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

void test_strong_numeric_identity() {
    static_assert(!std::is_same_v<RunId, WorkflowId>);
    static_assert(!std::is_same_v<WorkflowNodeId, AgentId>);
    static_assert(!std::is_convertible_v<std::size_t, RunId>);
    static_assert(!std::is_convertible_v<RunId, std::size_t>);

    const RunId run{3};
    const WorkflowId workflow{3};
    check(run.index() == 3, "run id exposes numeric index");
    check(workflow.index() == 3, "workflow id exposes numeric index");
}

void test_flat_store_assigns_event_ids() {
    ExecutionEventStore store;
    const RunId run{0};

    const auto first = store.append(RunStarted{.run = run}, 0ns);
    const auto second =
        store.append(RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 7ns);

    check(first == ExecutionEventId{0}, "first event id is zero");
    check(second == ExecutionEventId{1}, "second event id follows append order");
    check(store.size() == 2, "flat store size follows append count");
    check(store.find(first) != nullptr, "flat store resolves valid event id");
    check(store.find(ExecutionEventId{2}) == nullptr, "flat store rejects out of bounds id");
    check(std::holds_alternative<RunStarted>(store.events()[0].payload),
          "event payload is a variant");
    check(store.events()[1].monotonic_offset == 7ns, "event stores monotonic offset");
}

ExecutionEventStore make_complete_lifecycle_store() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId first_node{0};
    const WorkflowNodeId skipped_node{1};
    const AgentId agent{0};
    const AgentStateId state{0};
    const CapabilityId capability{0};
    const ProviderId primary{0};
    const ProviderId fallback{1};
    const InvocationId first_attempt{0};
    const InvocationId second_attempt{1};
    const RuntimeValueId value{0};
    const DiagnosticId diagnostic{0};
    const CheckpointId checkpoint{0};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, RunResumed{.run = run, .checkpoint = checkpoint}, 1ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 2ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = first_node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 3ns);
    append_event(store, NodeStarted{.node = first_node, .agent = agent}, 4ns);
    append_event(store,
                 AgentStateEntered{
                     .node = first_node,
                     .agent = agent,
                     .state = state,
                 },
                 5ns);
    append_event(store,
                 CapabilityStarted{
                     .invocation = first_attempt,
                     .node = first_node,
                     .capability = capability,
                     .provider = primary,
                     .attempt = 1,
                 },
                 6ns);
    append_event(store,
                 CapabilityFailed{
                     .invocation = first_attempt,
                     .kind = CapabilityFailureKind::Timeout,
                     .diagnostic = diagnostic,
                     .attempts = 1,
                     .retryable = true,
                 },
                 7ns);
    append_event(store,
                 CapabilityRetryScheduled{
                     .previous_invocation = first_attempt,
                     .next_invocation = second_attempt,
                     .next_attempt = 2,
                 },
                 8ns);
    append_event(store,
                 ProviderDegraded{
                     .invocation = second_attempt,
                     .provider = primary,
                     .fallback_provider = fallback,
                     .reason = ProviderDegradationReason::RetryExhausted,
                 },
                 9ns);
    append_event(store,
                 CapabilityStarted{
                     .invocation = second_attempt,
                     .node = first_node,
                     .capability = capability,
                     .provider = fallback,
                     .attempt = 2,
                 },
                 10ns);
    append_event(store,
                 CapabilityUsageRecorded{
                     .invocation = second_attempt,
                     .prompt_tokens = 16,
                     .completion_tokens = 4,
                     .total_tokens = 20,
                 },
                 11ns);
    append_event(store,
                 CapabilityCompleted{
                     .invocation = second_attempt,
                     .output = value,
                     .attempts = 2,
                     .cache_hit = false,
                 },
                 12ns);
    append_event(store, NodeCompleted{.node = first_node, .output = value}, 13ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = skipped_node,
                     .dependencies = {first_node},
                     .execution_slot = 1,
                 },
                 14ns);
    append_event(store,
                 NodeSkipped{
                     .node = skipped_node,
                     .blocking_dependencies = {first_node},
                 },
                 15ns);
    append_event(store, WorkflowCompleted{.workflow = workflow, .output = value}, 16ns);
    append_event(store, CheckpointSaved{.run = run, .checkpoint = checkpoint}, 17ns);
    append_event(store, RunCancellationRequested{.run = run}, 18ns);
    append_event(store, RunInterrupted{.run = run}, 19ns);
    append_event(
        store, RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 20ns);
    return store;
}

void test_restored_node_is_a_terminal_lifecycle_event() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId node{0};
    const AgentId agent{0};
    const RuntimeValueId value{0};
    const CheckpointId checkpoint{7};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, RunResumed{.run = run, .checkpoint = checkpoint}, 1ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 2ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 3ns);
    append_event(store,
                 NodeRestored{
                     .node = node,
                     .agent = agent,
                     .output = value,
                     .checkpoint = checkpoint,
                 },
                 4ns);
    append_event(store, WorkflowCompleted{.workflow = workflow, .output = value}, 5ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 6ns);

    const auto validation = validate_execution_events(store.events());
    check(validation.ok(), "restored node terminal validates");
}

void test_complete_lifecycle_validates() {
    auto store = make_complete_lifecycle_store();
    const auto validation = validate_execution_events(store.events());
    check(validation.ok(), "complete lifecycle validates");
    check(validation.issues.empty(), "complete lifecycle has no issues");
}

void test_missing_terminal_is_rejected() {
    ExecutionEventStore store;
    append_event(store, RunStarted{.run = RunId{0}}, 0ns);

    const auto validation = validate_execution_events(store.events());
    check(!validation.ok(), "missing run terminal is rejected");
    check(validation.has_issue(ExecutionEventValidationIssueKind::MissingTerminal),
          "missing terminal issue is classified");
}

void test_duplicate_terminal_is_rejected() {
    ExecutionEventStore store;
    const RunId run{0};
    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 1ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Failed}, 2ns);

    const auto validation = validate_execution_events(store.events());
    check(!validation.ok(), "duplicate run terminal is rejected");
    check(validation.has_issue(ExecutionEventValidationIssueKind::DuplicateTerminal),
          "duplicate terminal issue is classified");
}

void test_orphan_terminal_is_rejected() {
    ExecutionEventStore store;
    append_event(store,
                 NodeFailed{
                     .node = WorkflowNodeId{0},
                     .diagnostic = DiagnosticId{0},
                     .kind = NodeFailureKind::BudgetRejected,
                 },
                 0ns);

    const auto validation = validate_execution_events(store.events());
    check(!validation.ok(), "orphan node terminal is rejected");
    check(validation.has_issue(ExecutionEventValidationIssueKind::TerminalWithoutStart),
          "orphan terminal issue is classified");
}

void test_usage_after_invocation_terminal_is_rejected() {
    ExecutionEventStore store;
    append_event(store,
                 CapabilityStarted{
                     .invocation = InvocationId{0},
                     .node = WorkflowNodeId{0},
                     .capability = CapabilityId{0},
                     .provider = ProviderId{0},
                     .attempt = 1,
                 },
                 0ns);
    append_event(store,
                 CapabilityCompleted{
                     .invocation = InvocationId{0},
                     .attempts = 1,
                 },
                 1ns);
    append_event(store,
                 CapabilityUsageRecorded{
                     .invocation = InvocationId{0},
                     .total_tokens = 20,
                 },
                 2ns);

    const auto validation = validate_execution_events(store.events());
    check(!validation.ok(), "usage after terminal is rejected");
    check(validation.has_issue(ExecutionEventValidationIssueKind::UsageOutsideInvocation),
          "usage outside invocation issue is classified");
}

void test_duplicate_usage_is_rejected() {
    ExecutionEventStore store;
    append_event(store,
                 CapabilityStarted{
                     .invocation = InvocationId{0},
                     .node = WorkflowNodeId{0},
                     .capability = CapabilityId{0},
                     .provider = ProviderId{0},
                     .attempt = 1,
                 },
                 0ns);
    append_event(store,
                 CapabilityUsageRecorded{
                     .invocation = InvocationId{0},
                     .total_tokens = 20,
                 },
                 1ns);
    append_event(store,
                 CapabilityUsageRecorded{
                     .invocation = InvocationId{0},
                     .total_tokens = 20,
                 },
                 2ns);
    append_event(store,
                 CapabilityCompleted{
                     .invocation = InvocationId{0},
                     .attempts = 1,
                 },
                 3ns);

    const auto validation = validate_execution_events(store.events());
    check(!validation.ok(), "duplicate usage is rejected");
    check(validation.has_issue(ExecutionEventValidationIssueKind::DuplicateUsage),
          "duplicate usage issue is classified");
}

} // namespace

int main() {
    test_strong_numeric_identity();
    test_flat_store_assigns_event_ids();
    test_complete_lifecycle_validates();
    test_restored_node_is_a_terminal_lifecycle_event();
    test_missing_terminal_is_rejected();
    test_duplicate_terminal_is_rejected();
    test_orphan_terminal_is_rejected();
    test_usage_after_invocation_terminal_is_rejected();
    test_duplicate_usage_is_rejected();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

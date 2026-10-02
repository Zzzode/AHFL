// WH-5c.8 (GAP 8): unit tests for the shared capability event projection
// helper (capability_event_projection.hpp). The helper is the SINGLE
// implementation of the evaluator's aggregate->per-attempt event synthesis,
// called by BOTH the evaluator (workflow_runtime.cpp) and the wasm lanes
// (wasm_lifecycle.cpp). These tests pin the event ORDINALS/order (not just
// kinds) so a synthesis regression in either lane is caught here.

#include "runtime/engine/capability_event_projection.hpp"

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

[[nodiscard]] std::chrono::nanoseconds zero_clock() {
    return std::chrono::nanoseconds{0};
}

// Set up a WorkflowResult with one workflow / node / capability / provider so
// the helper has valid ids to emit.
struct ProjectionFixture {
    WorkflowResult result;
    WorkflowNodeId node;
    CapabilityId capability;
    ProviderId provider;

    ProjectionFixture() {
        const auto workflow = result.metadata.add_workflow("wf");
        const auto agent = result.metadata.add_agent("agent");
        node = result.metadata.add_node("node", workflow, agent);
        capability = result.metadata.add_capability("cap");
        provider = result.metadata.add_provider("runtime");
    }
};

void test_attempts_one_success() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.value = make_int(7);
    call.attempts = 1;
    const auto output_id = RuntimeValueId{0};
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call, output_id,
                                   InvocationId{});
    const auto &events = fx.result.events.events();
    check(events.size() == 2, "attempts1_success.event_count");
    check(std::holds_alternative<CapabilityStarted>(events[0].payload),
          "attempts1_success.ordinal0_started");
    const auto *started = std::get_if<CapabilityStarted>(&events[0].payload);
    check(started != nullptr && started->attempt == 1,
          "attempts1_success.started_attempt_1");
    check(started != nullptr && started->node == fx.node,
          "attempts1_success.started_node");
    check(started != nullptr && started->capability == fx.capability,
          "attempts1_success.started_capability");
    check(started != nullptr && started->provider == fx.provider,
          "attempts1_success.started_provider");
    check(std::holds_alternative<CapabilityCompleted>(events[1].payload),
          "attempts1_success.ordinal1_completed");
    const auto *completed =
        std::get_if<CapabilityCompleted>(&events[1].payload);
    check(completed != nullptr && completed->output == output_id,
          "attempts1_success.completed_output");
    check(completed != nullptr && completed->attempts == 1,
          "attempts1_success.completed_attempts");
    check(completed != nullptr && !completed->cache_hit,
          "attempts1_success.completed_cache_hit");
}

void test_attempts_two_retry_ordering() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.value = make_int(7);
    call.attempts = 2;
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    const auto &events = fx.result.events.events();
    // Ordinal pin (mirrors the evaluator's synthesis loop): a non-terminal
    // attempt that eventually succeeds still emits a retryable Failed.
    // Started(1), Failed(1, retryable), RetryScheduled, Started(2), Completed.
    check(events.size() == 5, "attempts2_retry.event_count");
    check(std::holds_alternative<CapabilityStarted>(events[0].payload),
          "attempts2_retry.ordinal0_started");
    check(std::holds_alternative<CapabilityFailed>(events[1].payload),
          "attempts2_retry.ordinal1_failed");
    check(std::holds_alternative<CapabilityRetryScheduled>(events[2].payload),
          "attempts2_retry.ordinal2_retry");
    check(std::holds_alternative<CapabilityStarted>(events[3].payload),
          "attempts2_retry.ordinal3_started");
    check(std::holds_alternative<CapabilityCompleted>(events[4].payload),
          "attempts2_retry.ordinal4_completed");

    const auto *started1 = std::get_if<CapabilityStarted>(&events[0].payload);
    const auto *failed1 = std::get_if<CapabilityFailed>(&events[1].payload);
    const auto *retry =
        std::get_if<CapabilityRetryScheduled>(&events[2].payload);
    const auto *started2 = std::get_if<CapabilityStarted>(&events[3].payload);
    const auto *completed =
        std::get_if<CapabilityCompleted>(&events[4].payload);

    check(started1 != nullptr && started1->attempt == 1,
          "attempts2_retry.started1_attempt");
    check(started2 != nullptr && started2->attempt == 2,
          "attempts2_retry.started2_attempt");
    check(started1 != nullptr && started2 != nullptr &&
              started1->invocation != started2->invocation,
          "attempts2_retry.distinct_invocation_ids");
    check(failed1 != nullptr && failed1->attempts == 1,
          "attempts2_retry.failed1_attempts");
    check(failed1 != nullptr && failed1->retryable,
          "attempts2_retry.failed1_retryable");
    check(retry != nullptr && retry->previous_invocation == started1->invocation,
          "attempts2_retry.retry_previous");
    check(retry != nullptr && retry->next_invocation == started2->invocation,
          "attempts2_retry.retry_next");
    check(retry != nullptr && retry->next_attempt == 2,
          "attempts2_retry.retry_next_attempt");
    check(completed != nullptr && completed->invocation == started2->invocation,
          "attempts2_retry.completed_last_invocation");
    check(completed != nullptr && completed->attempts == 2,
          "attempts2_retry.completed_attempts");
}

void test_preallocated_first_invocation() {
    ProjectionFixture fx;
    const auto prealloc =
        fx.result.metadata.add_invocation(fx.node, fx.capability);
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.attempts = 1;
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, prealloc);
    const auto &events = fx.result.events.events();
    check(events.size() == 2, "prealloc_invocation.event_count");
    const auto *started = std::get_if<CapabilityStarted>(&events[0].payload);
    check(started != nullptr && started->invocation == prealloc,
          "prealloc_invocation.started_uses_prealloc");
}

void test_provider_degraded() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.attempts = 1;
    call.provider_degraded = true;
    call.degraded_provider_name = "primary";
    call.selected_provider_name = "backup";
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    const auto &events = fx.result.events.events();
    // Ordinal pin: Started, ProviderDegraded, Completed.
    check(events.size() == 3, "degraded.event_count");
    check(std::holds_alternative<CapabilityStarted>(events[0].payload),
          "degraded.ordinal0_started");
    check(std::holds_alternative<ProviderDegraded>(events[1].payload),
          "degraded.ordinal1_provider_degraded");
    check(std::holds_alternative<CapabilityCompleted>(events[2].payload),
          "degraded.ordinal2_completed");
    const auto *degraded = std::get_if<ProviderDegraded>(&events[1].payload);
    check(degraded != nullptr &&
              degraded->reason == ProviderDegradationReason::RetryExhausted,
          "degraded.reason");
    check(degraded != nullptr && degraded->provider != degraded->fallback_provider,
          "degraded.distinct_provider_ids");
    const auto *degraded_meta =
        fx.result.metadata.provider(degraded->provider);
    const auto *selected_meta =
        fx.result.metadata.provider(degraded->fallback_provider);
    check(degraded_meta != nullptr && degraded_meta->display_name == "primary",
          "degraded.degraded_name");
    check(selected_meta != nullptr && selected_meta->display_name == "backup",
          "degraded.selected_name");
}

void test_failure_no_completed() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Error;
    call.error_message = "boom";
    call.attempts = 1;
    call.failure_kind = CapabilityFailureKind::Error;
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    const auto &events = fx.result.events.events();
    // Ordinal pin: Started, Failed. NO Completed.
    check(events.size() == 2, "failure.event_count");
    check(std::holds_alternative<CapabilityStarted>(events[0].payload),
          "failure.ordinal0_started");
    check(std::holds_alternative<CapabilityFailed>(events[1].payload),
          "failure.ordinal1_failed");
    bool has_completed = false;
    for (const auto &event : events) {
        if (std::holds_alternative<CapabilityCompleted>(event.payload)) {
            has_completed = true;
        }
    }
    check(!has_completed, "failure.no_completed");
    const auto *failed = std::get_if<CapabilityFailed>(&events[1].payload);
    check(failed != nullptr && failed->kind == CapabilityFailureKind::Error,
          "failure.kind");
    check(failed != nullptr && !failed->diagnostic.has_value(),
          "failure.diagnostic_nullopt");
}

void test_budget_fail_carries_kind() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Error;
    call.error_message = "prompt budget rejected";
    call.attempts = 1;
    call.failure_kind = CapabilityFailureKind::BudgetRejected;
    call.diagnostic_code = "runtime.LLM_TOKEN_BUDGET_EXCEEDED";
    call.usage = CapabilityUsage{
        .prompt_tokens = 16,
        .completion_tokens = 4,
        .total_tokens = 20,
        .total_cost_usd = 0.000012,
        .cost_estimated = true,
    };
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    const auto &events = fx.result.events.events();
    // Ordinal pin: Started, UsageRecorded, Failed. NO Completed.
    check(events.size() == 3, "budget_fail.event_count");
    check(std::holds_alternative<CapabilityStarted>(events[0].payload),
          "budget_fail.ordinal0_started");
    check(std::holds_alternative<CapabilityUsageRecorded>(events[1].payload),
          "budget_fail.ordinal1_usage");
    check(std::holds_alternative<CapabilityFailed>(events[2].payload),
          "budget_fail.ordinal2_failed");
    const auto *failed = std::get_if<CapabilityFailed>(&events[2].payload);
    check(failed != nullptr &&
              failed->kind == CapabilityFailureKind::BudgetRejected,
          "budget_fail.kind");
}

void test_multiple_notices_warning_diagnostics() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.attempts = 1;
    call.usage = CapabilityUsage{
        .prompt_tokens = 100,
        .completion_tokens = 10,
        .total_tokens = 110,
        .total_cost_usd = 0.0,
        .cost_estimated = false,
        .notices =
            {
                CapabilityPolicyNotice{
                    .diagnostic_code = "runtime.LLM_TOKEN_BUDGET_EXCEEDED",
                    .message = "token budget warning one",
                },
                CapabilityPolicyNotice{
                    .diagnostic_code = "runtime.LLM_COST_BUDGET_EXCEEDED",
                    .message = "cost budget warning two",
                },
            },
    };
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    check(fx.result.diagnostics.warning_count() == 2,
          "notices.warning_count");
    const auto &entries = fx.result.diagnostics.entries();
    check(entries.size() == 2, "notices.entry_count");
    if (entries.size() == 2) {
        check(entries[0].severity == DiagnosticSeverity::Warning,
              "notices.entry0_severity");
        check(entries[0].code.value_or("") ==
                  "runtime.LLM_TOKEN_BUDGET_EXCEEDED",
              "notices.entry0_code");
        check(entries[0].message == "token budget warning one",
              "notices.entry0_message");
        check(entries[1].severity == DiagnosticSeverity::Warning,
              "notices.entry1_severity");
        check(entries[1].code.value_or("") ==
                  "runtime.LLM_COST_BUDGET_EXCEEDED",
              "notices.entry1_code");
        check(entries[1].message == "cost budget warning two",
              "notices.entry1_message");
    }
}

void test_pending_emits_nothing() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Pending;
    call.attempts = 1;
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    check(fx.result.events.events().empty(), "pending.zero_events");
    check(fx.result.diagnostics.entries().empty(),
          "pending.zero_diagnostics");
}

void test_usage_recorded_on_terminal_attempt_only() {
    ProjectionFixture fx;
    CapabilityCallResult call;
    call.status = CapabilityCallStatus::Success;
    call.attempts = 3;
    call.usage = CapabilityUsage{
        .prompt_tokens = 16,
        .completion_tokens = 4,
        .total_tokens = 20,
    };
    project_capability_call_events(fx.result, zero_clock, fx.node,
                                   fx.capability, fx.provider, call,
                                   std::nullopt, InvocationId{});
    const auto &events = fx.result.events.events();
    // Started(1), Failed(1), Retry, Started(2), Failed(2), Retry,
    // Started(3), UsageRecorded, Completed. UsageRecorded fires once, on the
    // terminal attempt only, and immediately precedes Completed.
    std::size_t usage_count = 0;
    for (const auto &event : events) {
        if (std::holds_alternative<CapabilityUsageRecorded>(event.payload)) {
            ++usage_count;
        }
    }
    check(usage_count == 1, "usage_terminal_only.count");
    // The UsageRecorded must immediately precede Completed (ordinal pin).
    bool usage_before_completed = false;
    for (std::size_t i = 0; i + 1 < events.size(); ++i) {
        if (std::holds_alternative<CapabilityUsageRecorded>(events[i].payload) &&
            std::holds_alternative<CapabilityCompleted>(events[i + 1].payload)) {
            usage_before_completed = true;
        }
    }
    check(usage_before_completed, "usage_terminal_only.ordinal");
}

} // namespace

int main() {
    test_attempts_one_success();
    test_attempts_two_retry_ordering();
    test_preallocated_first_invocation();
    test_provider_degraded();
    test_failure_no_completed();
    test_budget_fail_carries_kind();
    test_multiple_notices_warning_diagnostics();
    test_pending_emits_nothing();
    test_usage_recorded_on_terminal_attempt_only();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}

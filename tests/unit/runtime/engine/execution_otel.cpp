#include "ahfl/runtime/execution_otel.hpp"

#include "ahfl/runtime/execution_event.hpp"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>

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
                  std::chrono::nanoseconds offset) {
    (void)store.append(std::move(payload), offset);
}

ExecutionEventStore make_trace() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId node{0};
    const AgentId agent{0};
    const CapabilityId capability{0};
    const ProviderId provider{0};
    const InvocationId invocation{0};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 1ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(store, NodeStarted{.node = node, .agent = agent}, 3ns);
    append_event(store,
                 CapabilityStarted{
                     .invocation = invocation,
                     .node = node,
                     .capability = capability,
                     .provider = provider,
                     .attempt = 1,
                 },
                 4ns);
    append_event(store,
                 CapabilityUsageRecorded{
                     .invocation = invocation,
                     .prompt_tokens = 16,
                     .completion_tokens = 4,
                     .total_tokens = 20,
                     .total_cost_usd = 0.000012,
                     .cost_estimated = true,
                 },
                 5ns);
    append_event(store,
                 CapabilityCompleted{
                     .invocation = invocation,
                     .output = std::nullopt,
                     .attempts = 1,
                     .cache_hit = false,
                 },
                 6ns);
    append_event(store, NodeCompleted{.node = node, .output = std::nullopt}, 7ns);
    append_event(store, WorkflowCompleted{.workflow = workflow, .output = std::nullopt}, 8ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 9ns);
    return store;
}

ExecutionEventStore make_retry_trace() {
    ExecutionEventStore store;
    const RunId run{0};
    const WorkflowId workflow{0};
    const WorkflowNodeId node{0};
    const AgentId agent{0};
    const CapabilityId capability{0};
    const ProviderId primary{0};
    const ProviderId fallback{1};
    const InvocationId first{0};
    const InvocationId second{1};

    append_event(store, RunStarted{.run = run}, 0ns);
    append_event(store, WorkflowStarted{.run = run, .workflow = workflow}, 1ns);
    append_event(store,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(store, NodeStarted{.node = node, .agent = agent}, 3ns);
    append_event(store,
                 CapabilityStarted{
                     .invocation = first,
                     .node = node,
                     .capability = capability,
                     .provider = primary,
                     .attempt = 1,
                 },
                 4ns);
    append_event(store,
                 CapabilityFailed{
                     .invocation = first,
                     .kind = CapabilityFailureKind::Timeout,
                     .diagnostic = std::nullopt,
                     .attempts = 1,
                     .retryable = true,
                 },
                 5ns);
    append_event(store,
                 CapabilityRetryScheduled{
                     .previous_invocation = first,
                     .next_invocation = second,
                     .next_attempt = 2,
                 },
                 6ns);
    append_event(store,
                 ProviderDegraded{
                     .invocation = second,
                     .provider = primary,
                     .fallback_provider = fallback,
                     .reason = ProviderDegradationReason::Timeout,
                 },
                 7ns);
    append_event(store,
                 CapabilityStarted{
                     .invocation = second,
                     .node = node,
                     .capability = capability,
                     .provider = fallback,
                     .attempt = 2,
                 },
                 8ns);
    append_event(store,
                 CapabilityCompleted{
                     .invocation = second,
                     .output = std::nullopt,
                     .attempts = 2,
                     .cache_hit = false,
                 },
                 9ns);
    append_event(store, NodeCompleted{.node = node, .output = std::nullopt}, 10ns);
    append_event(store, WorkflowCompleted{.workflow = workflow, .output = std::nullopt}, 11ns);
    append_event(store, RunCompleted{.run = run, .status = RunTerminalStatus::Completed}, 12ns);
    return store;
}

void test_otel_projection_builds_parented_spans() {
    const auto store = make_trace();
    const auto base =
        std::chrono::system_clock::time_point{std::chrono::system_clock::duration{123456}};
    const auto base_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(base.time_since_epoch()).count();
    const auto trace = build_execution_otel_trace(store.events(), base);

    check(trace.has_value(), "OTel projection builds");
    if (!trace.has_value()) {
        return;
    }
    check(trace->spans.size() == 4, "run workflow node and capability spans emitted");
    check(trace->spans[0].name == "ahfl.run", "run span name");
    check(trace->spans[1].parent_span_id == trace->spans[0].span_id,
          "workflow parent is run");
    check(trace->spans[2].parent_span_id == trace->spans[1].span_id,
          "node parent is workflow");
    check(trace->spans[3].parent_span_id == trace->spans[2].span_id,
          "capability parent is node");
    check(trace->spans[0].start_time_unix_nano == static_cast<std::uint64_t>(base_ns),
          "run span starts at supplied wall clock");
    check(trace->spans[0].end_time_unix_nano == static_cast<std::uint64_t>(base_ns + 9),
          "run span ends using monotonic offset");
    check(trace->spans[3].kind == ExecutionOtelSpanKind::Client,
          "capability span is client kind");
    check(trace->spans[3].status == ExecutionOtelStatus::Ok,
          "completed capability span is OK");
    bool saw_total_tokens = false;
    for (const auto &[key, value] : trace->spans[3].attributes) {
        if (key == "ahfl.usage.total_tokens" && value == "20") {
            saw_total_tokens = true;
        }
    }
    check(saw_total_tokens, "capability span carries canonical usage");
}

void test_otel_json_is_otlp_compatible() {
    const auto store = make_trace();
    const auto trace =
        build_execution_otel_trace(store.events(), std::chrono::system_clock::time_point{});
    check(trace.has_value(), "OTel trace builds for JSON");
    if (!trace.has_value()) {
        return;
    }

    std::ostringstream out;
    render_execution_otel_json(*trace, out);
    const auto json = out.str();
    check(json.find("\"resourceSpans\"") != std::string::npos,
          "OTLP JSON has resourceSpans");
    check(json.find("\"scopeSpans\"") != std::string::npos,
          "OTLP JSON has scopeSpans");
    check(json.find("\"traceId\"") != std::string::npos, "OTLP JSON has traceId");
    check(json.find("\"spanId\"") != std::string::npos, "OTLP JSON has spanId");
    check(json.find("\"startTimeUnixNano\"") != std::string::npos,
          "OTLP JSON has start time");
    check(json.find("\"endTimeUnixNano\"") != std::string::npos,
          "OTLP JSON has end time");
}

void test_otel_projection_rejects_invalid_lifecycle() {
    ExecutionEventStore store;
    append_event(store, RunStarted{.run = RunId{0}}, 0ns);
    const auto trace =
        build_execution_otel_trace(store.events(), std::chrono::system_clock::time_point{});
    check(!trace.has_value(), "OTel projection rejects invalid lifecycle");
    if (!trace.has_value()) {
        check(trace.error() == ExecutionOtelError::InvalidEventStream,
              "invalid lifecycle error classified");
    }
}

void test_otel_projection_handles_retry_and_prestart_degradation() {
    const auto store = make_retry_trace();
    const auto trace =
        build_execution_otel_trace(store.events(), std::chrono::system_clock::time_point{});
    check(trace.has_value(), "OTel projection handles retry and fallback ordering");
    if (!trace.has_value()) {
        return;
    }
    check(trace->spans.size() == 5, "retry emits two capability spans");
    const auto &node = trace->spans[2];
    check(node.events.size() == 2, "retry and degradation attach to node span");
    check(node.events[0].name == "ahfl.capability.retry.scheduled",
          "retry event name");
    check(node.events[1].name == "ahfl.provider.degraded",
          "degradation event name");
    check(trace->spans[3].status == ExecutionOtelStatus::Error,
          "first capability attempt is error");
    check(trace->spans[4].status == ExecutionOtelStatus::Ok,
          "fallback capability attempt is OK");
}

} // namespace

int main() {
    test_otel_projection_builds_parented_spans();
    test_otel_json_is_otlp_compatible();
    test_otel_projection_rejects_invalid_lifecycle();
    test_otel_projection_handles_retry_and_prestart_degradation();
    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}

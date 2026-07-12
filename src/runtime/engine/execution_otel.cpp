#include "ahfl/runtime/execution_otel.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "base/json/json_value.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::runtime {
namespace {

using ahfl::json::JsonValue;

struct SpanState {
    std::size_t index{0};
    bool terminal{false};
};

enum class ProjectionStep {
    Continue,
    InvalidEventStream,
    MissingParentSpan,
};

[[nodiscard]] std::string hex_id(std::uint64_t value, std::size_t width) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << value;
    return out.str();
}

[[nodiscard]] std::string trace_id(RunId run) {
    return hex_id(0x6168666c72756e00ULL, 16) +
           hex_id(static_cast<std::uint64_t>(run.index()) + 1U, 16);
}

[[nodiscard]] std::string span_id(std::uint8_t domain, std::size_t identity) {
    constexpr std::uint64_t kIdentityMask = 0x00FFFFFFFFFFFFFFULL;
    const auto value = (static_cast<std::uint64_t>(domain) << 56U) |
                       ((static_cast<std::uint64_t>(identity) + 1U) & kIdentityMask);
    return hex_id(value, 16);
}

[[nodiscard]] std::optional<std::uint64_t>
unix_nanos(std::chrono::system_clock::time_point base, std::chrono::nanoseconds offset) {
    const auto base_count =
        std::chrono::duration_cast<std::chrono::nanoseconds>(base.time_since_epoch()).count();
    const auto offset_count = offset.count();
    if (base_count < 0 || offset_count < 0) {
        return std::nullopt;
    }
    if (offset_count > std::numeric_limits<std::int64_t>::max() - base_count) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(base_count + offset_count);
}

void add_attribute(ExecutionOtelAttributes &attributes,
                   std::string key,
                   std::size_t value) {
    attributes.emplace_back(std::move(key), std::to_string(value));
}

void add_attribute(ExecutionOtelAttributes &attributes,
                   std::string key,
                   std::string value) {
    attributes.emplace_back(std::move(key), std::move(value));
}

[[nodiscard]] ExecutionOtelSpan make_span(std::string trace,
                                          std::string id,
                                          std::string parent,
                                          std::string name,
                                          ExecutionOtelSpanKind kind,
                                          std::uint64_t start) {
    return ExecutionOtelSpan{
        .trace_id = std::move(trace),
        .span_id = std::move(id),
        .parent_span_id = std::move(parent),
        .name = std::move(name),
        .kind = kind,
        .status = ExecutionOtelStatus::Unset,
        .start_time_unix_nano = start,
        .end_time_unix_nano = start,
        .attributes = {},
        .events = {},
    };
}

void finish_span(ExecutionOtelTrace &trace,
                 SpanState &state,
                 std::uint64_t end,
                 ExecutionOtelStatus status) {
    auto &span = trace.spans[state.index];
    span.end_time_unix_nano = end;
    span.status = status;
    state.terminal = true;
}

void add_event(ExecutionOtelSpan &span,
               std::string name,
               std::uint64_t time,
               ExecutionOtelAttributes attributes = {}) {
    span.events.push_back(ExecutionOtelEvent{
        .name = std::move(name),
        .time_unix_nano = time,
        .attributes = std::move(attributes),
    });
}

[[nodiscard]] std::unique_ptr<JsonValue> string_value(std::string value) {
    auto object = JsonValue::make_object();
    object->set("stringValue", JsonValue::make_string(std::move(value)));
    return object;
}

[[nodiscard]] std::unique_ptr<JsonValue>
attribute_json(const std::pair<std::string, std::string> &attribute) {
    auto object = JsonValue::make_object();
    object->set("key", JsonValue::make_string(attribute.first));
    object->set("value", string_value(attribute.second));
    return object;
}

[[nodiscard]] std::unique_ptr<JsonValue> attributes_json(const ExecutionOtelAttributes &values) {
    auto attributes = JsonValue::make_array();
    for (const auto &attribute : values) {
        attributes->push(attribute_json(attribute));
    }
    return attributes;
}

[[nodiscard]] std::unique_ptr<JsonValue> event_json(const ExecutionOtelEvent &event) {
    auto object = JsonValue::make_object();
    object->set("name", JsonValue::make_string(event.name));
    object->set("timeUnixNano", JsonValue::make_string(std::to_string(event.time_unix_nano)));
    object->set("attributes", attributes_json(event.attributes));
    return object;
}

[[nodiscard]] std::unique_ptr<JsonValue> span_json(const ExecutionOtelSpan &span) {
    auto object = JsonValue::make_object();
    object->set("traceId", JsonValue::make_string(span.trace_id));
    object->set("spanId", JsonValue::make_string(span.span_id));
    if (!span.parent_span_id.empty()) {
        object->set("parentSpanId", JsonValue::make_string(span.parent_span_id));
    }
    object->set("name", JsonValue::make_string(span.name));
    object->set("kind",
                JsonValue::make_int(span.kind == ExecutionOtelSpanKind::Client ? 3 : 1));
    object->set(
        "startTimeUnixNano", JsonValue::make_string(std::to_string(span.start_time_unix_nano)));
    object->set("endTimeUnixNano",
                JsonValue::make_string(std::to_string(span.end_time_unix_nano)));
    object->set("attributes", attributes_json(span.attributes));
    auto events = JsonValue::make_array();
    for (const auto &event : span.events) {
        events->push(event_json(event));
    }
    object->set("events", std::move(events));
    auto status = JsonValue::make_object();
    status->set("code",
                JsonValue::make_int(span.status == ExecutionOtelStatus::Ok
                                        ? 1
                                        : span.status == ExecutionOtelStatus::Error ? 2 : 0));
    object->set("status", std::move(status));
    return object;
}

} // namespace

ExecutionOtelResult
build_execution_otel_trace(std::span<const ExecutionEvent> events,
                           std::chrono::system_clock::time_point run_wall_time) {
    if (!validate_execution_events(events).ok()) {
        return std::unexpected(ExecutionOtelError::InvalidEventStream);
    }

    ExecutionOtelTrace trace;
    std::optional<SpanState> run_span;
    std::map<std::size_t, SpanState> workflow_spans;
    std::map<std::size_t, SpanState> node_spans;
    std::map<std::size_t, SpanState> invocation_spans;
    std::map<std::size_t, std::size_t> node_workflows;
    std::map<std::size_t, std::uint64_t> node_schedule_times;
    std::map<std::size_t, std::size_t> invocation_nodes;

    for (const auto &event : events) {
        const auto time = unix_nanos(run_wall_time, event.monotonic_offset);
        if (!time.has_value()) {
            return std::unexpected(ExecutionOtelError::InvalidTimestamp);
        }

        const auto result = std::visit(
            ahfl::Overloaded{
                [&](const RunStarted &payload) -> ProjectionStep {
                    trace.trace_id = trace_id(payload.run);
                    auto span = make_span(trace.trace_id,
                                          span_id(1, payload.run.index()),
                                          {},
                                          "ahfl.run",
                                          ExecutionOtelSpanKind::Internal,
                                          *time);
                    add_attribute(span.attributes, "ahfl.run.id", payload.run.index());
                    trace.spans.push_back(std::move(span));
                    run_span = SpanState{.index = trace.spans.size() - 1};
                    return ProjectionStep::Continue;
                },
                [&](const RunResumed &payload) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    ExecutionOtelAttributes attributes;
                    add_attribute(attributes, "ahfl.checkpoint.id", payload.checkpoint.index());
                    add_event(trace.spans[run_span->index], "ahfl.run.resumed", *time,
                              std::move(attributes));
                    return ProjectionStep::Continue;
                },
                [&](const WorkflowStarted &payload) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    auto span = make_span(trace.trace_id,
                                          span_id(2, payload.workflow.index()),
                                          trace.spans[run_span->index].span_id,
                                          "ahfl.workflow",
                                          ExecutionOtelSpanKind::Internal,
                                          *time);
                    add_attribute(span.attributes, "ahfl.workflow.id", payload.workflow.index());
                    trace.spans.push_back(std::move(span));
                    workflow_spans.emplace(
                        payload.workflow.index(),
                        SpanState{.index = trace.spans.size() - 1});
                    return ProjectionStep::Continue;
                },
                [&](const NodeScheduled &payload) -> ProjectionStep {
                    node_workflows[payload.node.index()] = payload.workflow.index();
                    node_schedule_times[payload.node.index()] = *time;
                    return ProjectionStep::Continue;
                },
                [&](const NodeStarted &payload) -> ProjectionStep {
                    const auto owner = node_workflows.find(payload.node.index());
                    if (owner == node_workflows.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto parent = workflow_spans.find(owner->second);
                    if (parent == workflow_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    auto span = make_span(trace.trace_id,
                                          span_id(3, payload.node.index()),
                                          trace.spans[parent->second.index].span_id,
                                          "ahfl.workflow.node",
                                          ExecutionOtelSpanKind::Internal,
                                          *time);
                    add_attribute(span.attributes, "ahfl.node.id", payload.node.index());
                    add_attribute(span.attributes, "ahfl.agent.id", payload.agent.index());
                    trace.spans.push_back(std::move(span));
                    node_spans.emplace(payload.node.index(),
                                       SpanState{.index = trace.spans.size() - 1});
                    return ProjectionStep::Continue;
                },
                [&](const AgentStateEntered &payload) -> ProjectionStep {
                    const auto node = node_spans.find(payload.node.index());
                    if (node == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    ExecutionOtelAttributes attributes;
                    add_attribute(attributes, "ahfl.agent.id", payload.agent.index());
                    add_attribute(attributes, "ahfl.agent.state.id", payload.state.index());
                    add_event(trace.spans[node->second.index],
                              "ahfl.agent.state.entered",
                              *time,
                              std::move(attributes));
                    return ProjectionStep::Continue;
                },
                [&](const CapabilityStarted &payload) -> ProjectionStep {
                    const auto parent = node_spans.find(payload.node.index());
                    if (parent == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    auto span = make_span(trace.trace_id,
                                          span_id(4, payload.invocation.index()),
                                          trace.spans[parent->second.index].span_id,
                                          "ahfl.capability",
                                          ExecutionOtelSpanKind::Client,
                                          *time);
                    add_attribute(
                        span.attributes, "ahfl.invocation.id", payload.invocation.index());
                    add_attribute(span.attributes, "ahfl.node.id", payload.node.index());
                    add_attribute(
                        span.attributes, "ahfl.capability.id", payload.capability.index());
                    add_attribute(span.attributes, "ahfl.provider.id", payload.provider.index());
                    add_attribute(span.attributes, "ahfl.attempt", payload.attempt);
                    trace.spans.push_back(std::move(span));
                    invocation_spans.emplace(payload.invocation.index(),
                                             SpanState{.index = trace.spans.size() - 1});
                    invocation_nodes[payload.invocation.index()] = payload.node.index();
                    return ProjectionStep::Continue;
                },
                [&](const CapabilityUsageRecorded &payload) -> ProjectionStep {
                    const auto span = invocation_spans.find(payload.invocation.index());
                    if (span == invocation_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    auto &attributes = trace.spans[span->second.index].attributes;
                    add_attribute(attributes, "gen_ai.usage.input_tokens", payload.prompt_tokens);
                    add_attribute(
                        attributes, "gen_ai.usage.output_tokens", payload.completion_tokens);
                    add_attribute(attributes, "ahfl.usage.total_tokens", payload.total_tokens);
                    add_attribute(attributes,
                                  "ahfl.usage.total_cost_usd",
                                  std::to_string(payload.total_cost_usd));
                    add_attribute(attributes,
                                  "ahfl.usage.cost_estimated",
                                  payload.cost_estimated ? "true" : "false");
                    return ProjectionStep::Continue;
                },
                [&](const CapabilityCompleted &payload) -> ProjectionStep {
                    const auto span = invocation_spans.find(payload.invocation.index());
                    if (span == invocation_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    add_attribute(trace.spans[span->second.index].attributes,
                                  "ahfl.cache.hit",
                                  payload.cache_hit ? "true" : "false");
                    add_attribute(trace.spans[span->second.index].attributes,
                                  "ahfl.attempts",
                                  payload.attempts);
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Ok);
                    return ProjectionStep::Continue;
                },
                [&](const CapabilityFailed &payload) -> ProjectionStep {
                    const auto span = invocation_spans.find(payload.invocation.index());
                    if (span == invocation_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    add_attribute(trace.spans[span->second.index].attributes,
                                  "ahfl.retryable",
                                  payload.retryable ? "true" : "false");
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Error);
                    return ProjectionStep::Continue;
                },
                [&](const CapabilityRetryScheduled &payload) -> ProjectionStep {
                    const auto previous_node =
                        invocation_nodes.find(payload.previous_invocation.index());
                    if (previous_node == invocation_nodes.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto node = node_spans.find(previous_node->second);
                    if (node == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    invocation_nodes[payload.next_invocation.index()] = previous_node->second;
                    ExecutionOtelAttributes attributes;
                    add_attribute(
                        attributes, "ahfl.next.invocation.id", payload.next_invocation.index());
                    add_attribute(attributes, "ahfl.next.attempt", payload.next_attempt);
                    add_event(trace.spans[node->second.index],
                              "ahfl.capability.retry.scheduled",
                              *time,
                              std::move(attributes));
                    return ProjectionStep::Continue;
                },
                [&](const ProviderDegraded &payload) -> ProjectionStep {
                    const auto invocation_node = invocation_nodes.find(payload.invocation.index());
                    if (invocation_node == invocation_nodes.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto node = node_spans.find(invocation_node->second);
                    if (node == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    ExecutionOtelAttributes attributes;
                    add_attribute(attributes, "ahfl.provider.id", payload.provider.index());
                    add_attribute(
                        attributes, "ahfl.fallback.provider.id", payload.fallback_provider.index());
                    add_event(trace.spans[node->second.index],
                              "ahfl.provider.degraded",
                              *time,
                              std::move(attributes));
                    return ProjectionStep::Continue;
                },
                [&](const NodeCompleted &payload) -> ProjectionStep {
                    const auto span = node_spans.find(payload.node.index());
                    if (span == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Ok);
                    return ProjectionStep::Continue;
                },
                [&](const NodeRestored &payload) -> ProjectionStep {
                    const auto owner = node_workflows.find(payload.node.index());
                    if (owner == node_workflows.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto parent = workflow_spans.find(owner->second);
                    if (parent == workflow_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto scheduled = node_schedule_times.find(payload.node.index());
                    auto span = make_span(trace.trace_id,
                                          span_id(3, payload.node.index()),
                                          trace.spans[parent->second.index].span_id,
                                          "ahfl.workflow.node",
                                          ExecutionOtelSpanKind::Internal,
                                          scheduled == node_schedule_times.end() ? *time
                                                                                 : scheduled->second);
                    add_attribute(span.attributes, "ahfl.node.id", payload.node.index());
                    add_attribute(span.attributes, "ahfl.agent.id", payload.agent.index());
                    add_attribute(
                        span.attributes, "ahfl.restored.checkpoint.id", payload.checkpoint.index());
                    trace.spans.push_back(std::move(span));
                    auto [entry, inserted] = node_spans.emplace(
                        payload.node.index(), SpanState{.index = trace.spans.size() - 1});
                    if (!inserted) {
                        return ProjectionStep::InvalidEventStream;
                    }
                    finish_span(trace, entry->second, *time, ExecutionOtelStatus::Ok);
                    return ProjectionStep::Continue;
                },
                [&](const NodeFailed &payload) -> ProjectionStep {
                    const auto span = node_spans.find(payload.node.index());
                    if (span == node_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Error);
                    return ProjectionStep::Continue;
                },
                [&](const NodeSkipped &payload) -> ProjectionStep {
                    const auto owner = node_workflows.find(payload.node.index());
                    if (owner == node_workflows.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto parent = workflow_spans.find(owner->second);
                    if (parent == workflow_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto scheduled = node_schedule_times.find(payload.node.index());
                    auto span = make_span(trace.trace_id,
                                          span_id(3, payload.node.index()),
                                          trace.spans[parent->second.index].span_id,
                                          "ahfl.workflow.node",
                                          ExecutionOtelSpanKind::Internal,
                                          scheduled == node_schedule_times.end() ? *time
                                                                                 : scheduled->second);
                    add_attribute(span.attributes, "ahfl.node.id", payload.node.index());
                    add_attribute(span.attributes, "ahfl.node.status", std::string{"skipped"});
                    trace.spans.push_back(std::move(span));
                    auto [entry, inserted] = node_spans.emplace(
                        payload.node.index(), SpanState{.index = trace.spans.size() - 1});
                    if (!inserted) {
                        return ProjectionStep::InvalidEventStream;
                    }
                    finish_span(trace, entry->second, *time, ExecutionOtelStatus::Unset);
                    return ProjectionStep::Continue;
                },
                [&](const WorkflowCompleted &payload) -> ProjectionStep {
                    const auto span = workflow_spans.find(payload.workflow.index());
                    if (span == workflow_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Ok);
                    return ProjectionStep::Continue;
                },
                [&](const WorkflowFailed &payload) -> ProjectionStep {
                    const auto span = workflow_spans.find(payload.workflow.index());
                    if (span == workflow_spans.end()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    finish_span(trace, span->second, *time, ExecutionOtelStatus::Error);
                    return ProjectionStep::Continue;
                },
                [&](const CheckpointSaved &payload) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    ExecutionOtelAttributes attributes;
                    add_attribute(attributes, "ahfl.checkpoint.id", payload.checkpoint.index());
                    add_event(trace.spans[run_span->index],
                              "ahfl.checkpoint.saved",
                              *time,
                              std::move(attributes));
                    return ProjectionStep::Continue;
                },
                [&](const RunCancellationRequested &) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    add_event(trace.spans[run_span->index],
                              "ahfl.run.cancellation.requested",
                              *time);
                    return ProjectionStep::Continue;
                },
                [&](const RunInterrupted &) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    add_event(trace.spans[run_span->index], "ahfl.run.interrupted", *time);
                    return ProjectionStep::Continue;
                },
                [&](const RunCompleted &payload) -> ProjectionStep {
                    if (!run_span.has_value()) {
                        return ProjectionStep::MissingParentSpan;
                    }
                    const auto status = payload.status == RunTerminalStatus::Completed
                                            ? ExecutionOtelStatus::Ok
                                            : ExecutionOtelStatus::Error;
                    finish_span(trace, *run_span, *time, status);
                    return ProjectionStep::Continue;
                },
            },
            event.payload);
        if (result == ProjectionStep::MissingParentSpan) {
            return std::unexpected(ExecutionOtelError::MissingParentSpan);
        }
        if (result == ProjectionStep::InvalidEventStream) {
            return std::unexpected(ExecutionOtelError::InvalidEventStream);
        }
    }

    if (!run_span.has_value() || !run_span->terminal) {
        return std::unexpected(ExecutionOtelError::InvalidEventStream);
    }
    return trace;
}

void render_execution_otel_json(const ExecutionOtelTrace &trace, std::ostream &out) {
    auto root = JsonValue::make_object();
    auto resource_spans = JsonValue::make_array();
    auto resource_span = JsonValue::make_object();
    auto resource = JsonValue::make_object();
    resource->set(
        "attributes",
        attributes_json(ExecutionOtelAttributes{{"service.name", trace.service_name}}));
    resource_span->set("resource", std::move(resource));

    auto scope_spans = JsonValue::make_array();
    auto scope_span = JsonValue::make_object();
    auto scope = JsonValue::make_object();
    scope->set("name", JsonValue::make_string(trace.scope_name));
    scope_span->set("scope", std::move(scope));
    auto spans = JsonValue::make_array();
    for (const auto &span : trace.spans) {
        spans->push(span_json(span));
    }
    scope_span->set("spans", std::move(spans));
    scope_spans->push(std::move(scope_span));
    resource_span->set("scopeSpans", std::move(scope_spans));
    resource_spans->push(std::move(resource_span));
    root->set("resourceSpans", std::move(resource_spans));
    out << ahfl::json::serialize_json(*root) << '\n';
}

} // namespace ahfl::runtime

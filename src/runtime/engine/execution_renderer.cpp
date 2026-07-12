#include "ahfl/runtime/execution_renderer.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/diagnostic_serialization.hpp"
#include "ahfl/runtime/execution_projection.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

namespace ahfl::runtime {
namespace {

using ahfl::json::JsonValue;

[[nodiscard]] std::string_view run_status_name(RunTerminalStatus status) {
    switch (status) {
    case RunTerminalStatus::Completed:
        return "completed";
    case RunTerminalStatus::Failed:
        return "failed";
    case RunTerminalStatus::Cancelled:
        return "cancelled";
    case RunTerminalStatus::Interrupted:
        return "interrupted";
    }
    return "failed";
}

[[nodiscard]] std::string_view node_status_name(NodeReportStatus status) {
    switch (status) {
    case NodeReportStatus::Scheduled:
        return "scheduled";
    case NodeReportStatus::Running:
        return "running";
    case NodeReportStatus::Completed:
        return "completed";
    case NodeReportStatus::Failed:
        return "failed";
    case NodeReportStatus::Skipped:
        return "skipped";
    }
    return "failed";
}

[[nodiscard]] std::unique_ptr<JsonValue> id_json(std::size_t index) {
    return JsonValue::make_int(static_cast<std::int64_t>(index));
}

[[nodiscard]] std::unique_ptr<JsonValue> optional_id_json(std::optional<RuntimeValueId> id) {
    return id.has_value() ? id_json(id->index()) : JsonValue::make_null();
}

[[nodiscard]] const Diagnostic *diagnostic(const WorkflowResult &result, DiagnosticId id) {
    if (!id.valid() || id.index() >= result.diagnostics.entries().size()) {
        return nullptr;
    }
    return &result.diagnostics.entries()[id.index()];
}

void set_diagnostic_fields(JsonValue &object,
                           const WorkflowResult &result,
                           std::optional<DiagnosticId> id) {
    object.set("diagnostic_id",
               id.has_value() && id->valid() ? id_json(id->index()) : JsonValue::make_null());
    const auto *entry = id.has_value() ? diagnostic(result, *id) : nullptr;
    object.set("diagnostic",
               entry != nullptr ? serialize_diagnostic_json(*entry) : JsonValue::make_null());
}

[[nodiscard]] const WorkflowMetadata *workflow_metadata(const WorkflowResult &result) {
    return result.metadata.workflow(result.report.workflow);
}

[[nodiscard]] const WorkflowNodeMetadata *
node_metadata(const WorkflowResult &result, WorkflowNodeId node) {
    return result.metadata.node(node);
}

[[nodiscard]] std::unique_ptr<JsonValue> value_json(const evaluator::Value *value) {
    if (value == nullptr) {
        return JsonValue::make_null();
    }
    auto parsed = ahfl::json::parse_json(evaluator::value_to_json(*value));
    return parsed.has_value() && *parsed ? std::move(*parsed) : JsonValue::make_null();
}

[[nodiscard]] std::string_view event_type(const ExecutionEventPayload &payload) {
    return std::visit(
        ahfl::Overloaded{
            [](const RunStarted &) -> std::string_view { return "run_started"; },
            [](const RunResumed &) -> std::string_view { return "run_resumed"; },
            [](const WorkflowStarted &) -> std::string_view { return "workflow_started"; },
            [](const NodeScheduled &) -> std::string_view { return "node_scheduled"; },
            [](const NodeStarted &) -> std::string_view { return "node_started"; },
            [](const AgentStateEntered &) -> std::string_view { return "agent_state_entered"; },
            [](const CapabilityStarted &) -> std::string_view { return "capability_started"; },
            [](const CapabilityUsageRecorded &) -> std::string_view {
                return "capability_usage_recorded";
            },
            [](const CapabilityCompleted &) -> std::string_view {
                return "capability_completed";
            },
            [](const CapabilityFailed &) -> std::string_view { return "capability_failed"; },
            [](const CapabilityRetryScheduled &) -> std::string_view {
                return "capability_retry_scheduled";
            },
            [](const ProviderDegraded &) -> std::string_view { return "provider_degraded"; },
            [](const NodeCompleted &) -> std::string_view { return "node_completed"; },
            [](const NodeRestored &) -> std::string_view { return "node_restored"; },
            [](const NodeFailed &) -> std::string_view { return "node_failed"; },
            [](const NodeSkipped &) -> std::string_view { return "node_skipped"; },
            [](const WorkflowCompleted &) -> std::string_view {
                return "workflow_completed";
            },
            [](const WorkflowFailed &) -> std::string_view { return "workflow_failed"; },
            [](const CheckpointSaved &) -> std::string_view { return "checkpoint_saved"; },
            [](const RunCancellationRequested &) -> std::string_view {
                return "run_cancellation_requested";
            },
            [](const RunInterrupted &) -> std::string_view { return "run_interrupted"; },
            [](const RunCompleted &) -> std::string_view { return "run_completed"; },
        },
        payload);
}

[[nodiscard]] std::unique_ptr<JsonValue>
event_payload_json(const WorkflowResult &result, const ExecutionEventPayload &payload) {
    auto object = JsonValue::make_object();
    std::visit(
        ahfl::Overloaded{
            [&](const RunStarted &value) { object->set("run_id", id_json(value.run.index())); },
            [&](const RunResumed &value) {
                object->set("run_id", id_json(value.run.index()));
                object->set("checkpoint_id", id_json(value.checkpoint.index()));
            },
            [&](const WorkflowStarted &value) {
                object->set("run_id", id_json(value.run.index()));
                object->set("workflow_id", id_json(value.workflow.index()));
            },
            [&](const NodeScheduled &value) {
                object->set("workflow_id", id_json(value.workflow.index()));
                object->set("node_id", id_json(value.node.index()));
                object->set("execution_slot", id_json(value.execution_slot));
                auto dependencies = JsonValue::make_array();
                for (const auto dependency : value.dependencies) {
                    dependencies->push(id_json(dependency.index()));
                }
                object->set("dependencies", std::move(dependencies));
            },
            [&](const NodeStarted &value) {
                object->set("node_id", id_json(value.node.index()));
                object->set("agent_id", id_json(value.agent.index()));
            },
            [&](const AgentStateEntered &value) {
                object->set("node_id", id_json(value.node.index()));
                object->set("agent_id", id_json(value.agent.index()));
                object->set("state_id", id_json(value.state.index()));
            },
            [&](const CapabilityStarted &value) {
                object->set("invocation_id", id_json(value.invocation.index()));
                object->set("node_id", id_json(value.node.index()));
                object->set("capability_id", id_json(value.capability.index()));
                object->set("provider_id", id_json(value.provider.index()));
                object->set("attempt", id_json(value.attempt));
            },
            [&](const CapabilityUsageRecorded &value) {
                object->set("invocation_id", id_json(value.invocation.index()));
                object->set("prompt_tokens", id_json(value.prompt_tokens));
                object->set("completion_tokens", id_json(value.completion_tokens));
                object->set("total_tokens", id_json(value.total_tokens));
                object->set("total_cost_usd", JsonValue::make_float(value.total_cost_usd));
                object->set("cost_estimated", JsonValue::make_bool(value.cost_estimated));
                auto notices = JsonValue::make_array();
                for (const auto &notice : value.notices) {
                    auto item = JsonValue::make_object();
                    item->set("diagnostic_code",
                              JsonValue::make_string(notice.diagnostic_code));
                    item->set("message", JsonValue::make_string(notice.message));
                    notices->push(std::move(item));
                }
                object->set("notices", std::move(notices));
            },
            [&](const CapabilityCompleted &value) {
                object->set("invocation_id", id_json(value.invocation.index()));
                object->set("output_value_id", optional_id_json(value.output));
                object->set("attempts", id_json(value.attempts));
                object->set("cache_hit", JsonValue::make_bool(value.cache_hit));
            },
            [&](const CapabilityFailed &value) {
                object->set("invocation_id", id_json(value.invocation.index()));
                set_diagnostic_fields(*object, result, value.diagnostic);
                object->set("attempts", id_json(value.attempts));
                object->set("retryable", JsonValue::make_bool(value.retryable));
            },
            [&](const CapabilityRetryScheduled &value) {
                object->set("previous_invocation_id", id_json(value.previous_invocation.index()));
                object->set("next_invocation_id", id_json(value.next_invocation.index()));
                object->set("next_attempt", id_json(value.next_attempt));
            },
            [&](const ProviderDegraded &value) {
                object->set("invocation_id", id_json(value.invocation.index()));
                object->set("provider_id", id_json(value.provider.index()));
                object->set("fallback_provider_id", id_json(value.fallback_provider.index()));
            },
            [&](const NodeCompleted &value) {
                object->set("node_id", id_json(value.node.index()));
                object->set("output_value_id", optional_id_json(value.output));
            },
            [&](const NodeRestored &value) {
                object->set("node_id", id_json(value.node.index()));
                object->set("agent_id", id_json(value.agent.index()));
                object->set("output_value_id", optional_id_json(value.output));
                object->set("checkpoint_id", id_json(value.checkpoint.index()));
            },
            [&](const NodeFailed &value) {
                object->set("node_id", id_json(value.node.index()));
                set_diagnostic_fields(*object, result, value.diagnostic);
            },
            [&](const NodeSkipped &value) {
                object->set("node_id", id_json(value.node.index()));
                auto dependencies = JsonValue::make_array();
                for (const auto dependency : value.blocking_dependencies) {
                    dependencies->push(id_json(dependency.index()));
                }
                object->set("blocking_dependencies", std::move(dependencies));
            },
            [&](const WorkflowCompleted &value) {
                object->set("workflow_id", id_json(value.workflow.index()));
                object->set("output_value_id", optional_id_json(value.output));
            },
            [&](const WorkflowFailed &value) {
                object->set("workflow_id", id_json(value.workflow.index()));
                set_diagnostic_fields(*object, result, value.diagnostic);
            },
            [&](const CheckpointSaved &value) {
                object->set("run_id", id_json(value.run.index()));
                object->set("checkpoint_id", id_json(value.checkpoint.index()));
            },
            [&](const RunCancellationRequested &value) {
                object->set("run_id", id_json(value.run.index()));
            },
            [&](const RunInterrupted &value) {
                object->set("run_id", id_json(value.run.index()));
            },
            [&](const RunCompleted &value) {
                object->set("run_id", id_json(value.run.index()));
                object->set(
                    "status", JsonValue::make_string(std::string(run_status_name(value.status))));
            },
        },
        payload);
    return object;
}

[[nodiscard]] std::unique_ptr<JsonValue> report_json(const WorkflowResult &result) {
    auto root = JsonValue::make_object();
    root->set("schema", JsonValue::make_string("ahfl.run-report"));
    root->set("schema_version", JsonValue::make_int(1));

    auto run = JsonValue::make_object();
    run->set("id", id_json(result.report.run.index()));
    run->set("status",
             JsonValue::make_string(std::string(run_status_name(result.report.status))));
    root->set("run", std::move(run));

    auto workflow = JsonValue::make_object();
    workflow->set("id", id_json(result.report.workflow.index()));
    if (const auto *metadata = workflow_metadata(result); metadata != nullptr) {
        workflow->set("name", JsonValue::make_string(metadata->display_name));
    }
    root->set("workflow", std::move(workflow));

    auto nodes = JsonValue::make_array();
    for (const auto &node : result.report.nodes) {
        auto item = JsonValue::make_object();
        item->set("id", id_json(node.node.index()));
        item->set("agent_id", node.agent.valid() ? id_json(node.agent.index())
                                                  : JsonValue::make_null());
        item->set("status",
                  JsonValue::make_string(std::string(node_status_name(node.status))));
        item->set("execution_slot", id_json(node.execution_slot));
        if (const auto *metadata = node_metadata(result, node.node); metadata != nullptr) {
            item->set("name", JsonValue::make_string(metadata->display_name));
            if (const auto *agent = result.metadata.agent(metadata->agent); agent != nullptr) {
                item->set("agent", JsonValue::make_string(agent->display_name));
            }
        }
        item->set("output_value_id", optional_id_json(node.output));
        item->set("restored_from_checkpoint_id",
                  node.restored_from_checkpoint.has_value()
                      ? id_json(node.restored_from_checkpoint->index())
                      : JsonValue::make_null());
        nodes->push(std::move(item));
    }
    root->set("nodes", std::move(nodes));
    root->set("result", value_json(result.output()));
    auto usage = JsonValue::make_object();
    usage->set("records", id_json(result.report.usage.records));
    usage->set("prompt_tokens", id_json(result.report.usage.prompt_tokens));
    usage->set("completion_tokens", id_json(result.report.usage.completion_tokens));
    usage->set("total_tokens", id_json(result.report.usage.total_tokens));
    usage->set("total_cost_usd", JsonValue::make_float(result.report.usage.total_cost_usd));
    usage->set("cache_hits", id_json(result.report.usage.cache_hits));
    usage->set("degraded_providers", id_json(result.report.usage.degraded_providers));
    root->set("usage", std::move(usage));

    if (const auto replay = build_execution_replay_projection(result); replay.has_value()) {
        auto projection = JsonValue::make_object();
        projection->set("run_id", id_json(replay->run.index()));
        projection->set("workflow_id", id_json(replay->workflow.index()));
        projection->set(
            "status", JsonValue::make_string(std::string(run_status_name(replay->status))));
        auto order = JsonValue::make_array();
        for (const auto node : replay->execution_order) {
            order->push(id_json(node.index()));
        }
        projection->set("execution_order", std::move(order));
        auto replay_nodes = JsonValue::make_array();
        for (const auto &node : replay->nodes) {
            auto item = JsonValue::make_object();
            item->set("node_id", id_json(node.node.index()));
            item->set("scheduled", JsonValue::make_bool(node.scheduled));
            item->set("started", JsonValue::make_bool(node.started));
            item->set("restored", JsonValue::make_bool(node.restored));
            item->set("restored_from_checkpoint_id",
                      node.restored_from_checkpoint.has_value()
                          ? id_json(node.restored_from_checkpoint->index())
                          : JsonValue::make_null());
            item->set("terminal",
                      JsonValue::make_string(
                          node.terminal == ReplayNodeTerminal::Completed
                              ? "completed"
                              : node.terminal == ReplayNodeTerminal::Failed
                                    ? "failed"
                                    : node.terminal == ReplayNodeTerminal::Skipped ? "skipped"
                                                                                   : "pending"));
            replay_nodes->push(std::move(item));
        }
        projection->set("nodes", std::move(replay_nodes));
        root->set("replay", std::move(projection));
    }
    if (const auto audit = build_execution_audit_projection(result); audit.has_value()) {
        auto projection = JsonValue::make_object();
        projection->set("run_id", id_json(audit->run.index()));
        projection->set("workflow_id", id_json(audit->workflow.index()));
        projection->set("total_events", id_json(audit->total_events));
        projection->set("node_scheduled", id_json(audit->node_scheduled));
        projection->set("node_started", id_json(audit->node_started));
        projection->set("node_completed", id_json(audit->node_completed));
        projection->set("node_restored", id_json(audit->node_restored));
        projection->set("node_failed", id_json(audit->node_failed));
        projection->set("node_skipped", id_json(audit->node_skipped));
        projection->set("capability_started", id_json(audit->capability_started));
        projection->set("capability_usage_recorded",
                        id_json(audit->capability_usage_recorded));
        projection->set("capability_completed", id_json(audit->capability_completed));
        projection->set("capability_failed", id_json(audit->capability_failed));
        projection->set("prompt_tokens", id_json(audit->prompt_tokens));
        projection->set("completion_tokens", id_json(audit->completion_tokens));
        projection->set("total_tokens", id_json(audit->total_tokens));
        projection->set("total_cost_usd", JsonValue::make_float(audit->total_cost_usd));
        projection->set("cache_hits", id_json(audit->cache_hits));
        projection->set("provider_degraded", id_json(audit->provider_degraded));
        projection->set("workflow_completed", id_json(audit->workflow_completed));
        projection->set("workflow_failed", id_json(audit->workflow_failed));
        projection->set("checkpoints_saved", id_json(audit->checkpoints_saved));
        projection->set("terminal_invariant_holds",
                        JsonValue::make_bool(audit->terminal_invariant_holds));
        root->set("audit", std::move(projection));
    }
    return root;
}

ExecutionRenderResult render_human(const WorkflowResult &result, std::ostream &out) {
    const auto *workflow = workflow_metadata(result);
    out << (workflow != nullptr ? workflow->display_name : std::string{"<workflow>"}) << "  "
        << run_status_name(result.report.status) << "\n\nSteps\n";
    for (const auto &node : result.report.nodes) {
        const auto *metadata = node_metadata(result, node.node);
        out << "  " << node_status_name(node.status) << "  "
            << (metadata != nullptr ? metadata->display_name : std::string{"<node>"});
        if (metadata != nullptr) {
            if (const auto *agent = result.metadata.agent(metadata->agent); agent != nullptr) {
                out << "  " << agent->display_name;
            }
        }
        out << '\n';
    }

    out << "\nResult\n  ";
    if (const auto *value = result.output(); value != nullptr) {
        evaluator::print_value(*value, out);
    } else {
        out << "(none)";
    }
    out << "\n\n" << run_status_name(result.report.status) << ' ' << result.report.nodes.size()
        << '/' << result.report.nodes.size() << " nodes";
    if (result.report.usage.records != 0) {
        out << ", " << result.report.usage.records << " usage record";
        if (result.report.usage.records != 1) {
            out << 's';
        }
        out << ", " << result.report.usage.total_tokens << " tokens";
    }
    if (result.report.usage.cache_hits != 0) {
        out << ", " << result.report.usage.cache_hits << " cache hit";
        if (result.report.usage.cache_hits != 1) {
            out << 's';
        }
    }
    if (result.report.usage.degraded_providers != 0) {
        out << ", " << result.report.usage.degraded_providers << " provider degradation";
        if (result.report.usage.degraded_providers != 1) {
            out << 's';
        }
    }
    out << '\n';
    if (result.has_errors()) {
        result.diagnostics.render(out);
    }
    return {};
}

ExecutionRenderResult render_json_lines(const WorkflowResult &result, std::ostream &out) {
    for (const auto &event : result.events.events()) {
        auto root = JsonValue::make_object();
        root->set("schema", JsonValue::make_string("ahfl.run-event"));
        root->set("schema_version", JsonValue::make_int(1));
        root->set("event_id", id_json(event.id.index()));
        root->set("type", JsonValue::make_string(std::string(event_type(event.payload))));
        root->set("monotonic_offset_ns",
                  JsonValue::make_int(event.monotonic_offset.count()));
        root->set("payload", event_payload_json(result, event.payload));
        out << ahfl::json::serialize_json(*root) << '\n';
    }
    return {};
}

} // namespace

ExecutionRenderResult render_execution_result(const WorkflowResult &result,
                                              const ExecutionOutputOptions &options,
                                              std::ostream &out) {
    const auto validation = validate_execution_events(result.events.events());
    if (!validation.ok()) {
        return std::unexpected<std::string>(
            "cannot render an execution result with an invalid event lifecycle");
    }

    switch (options.format) {
    case ExecutionOutputFormat::Human:
        return render_human(result, out);
    case ExecutionOutputFormat::Json:
        out << ahfl::json::serialize_json(*report_json(result)) << '\n';
        return {};
    case ExecutionOutputFormat::JsonLines:
        return render_json_lines(result, out);
    case ExecutionOutputFormat::Quiet:
        if (const auto *value = result.output(); value != nullptr) {
            evaluator::write_value_json(*value, out);
            out << '\n';
        }
        return {};
    }
    return std::unexpected<std::string>("unknown execution output format");
}

} // namespace ahfl::runtime

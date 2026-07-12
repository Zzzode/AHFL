#include "ahfl/runtime/execution_report.hpp"

#include <map>

#include "ahfl/base/support/overloaded.hpp"

namespace ahfl::runtime {
namespace {

[[nodiscard]] ExecutionReportBuildResult
invalid_stream(ExecutionEventValidationResult validation) {
    return std::unexpected(ExecutionReportBuildError{
        .kind = ExecutionReportBuildIssueKind::InvalidEventStream,
        .validation = std::move(validation),
    });
}

[[nodiscard]] ExecutionReportBuildResult
missing_required(ExecutionReportBuildIssueKind kind) {
    return std::unexpected(ExecutionReportBuildError{.kind = kind});
}

} // namespace

ExecutionReportBuildResult build_execution_report(std::span<const ExecutionEvent> events) {
    auto validation = validate_execution_events(events);
    if (!validation.ok()) {
        return invalid_stream(std::move(validation));
    }

    ExecutionReport report;
    bool saw_run = false;
    bool saw_workflow = false;
    bool saw_run_terminal = false;
    std::map<WorkflowNodeId, std::size_t> node_indices;

    for (const auto &event : events) {
        auto outcome = std::visit(
            ahfl::Overloaded{
                [&](const RunStarted &payload) {
                    report.run = payload.run;
                    saw_run = true;
                    return true;
                },
                [&](const WorkflowStarted &payload) {
                    report.workflow = payload.workflow;
                    saw_workflow = true;
                    return true;
                },
                [&](const NodeScheduled &payload) {
                    node_indices.emplace(payload.node, report.nodes.size());
                    report.execution_order.push_back(payload.node);
                    report.nodes.push_back(ExecutionNodeReport{
                        .node = payload.node,
                        .execution_slot = payload.execution_slot,
                        .dependencies = payload.dependencies,
                    });
                    return true;
                },
                [&](const NodeStarted &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = report.nodes[found->second];
                    node.agent = payload.agent;
                    node.status = NodeReportStatus::Running;
                    return true;
                },
                [&](const NodeCompleted &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = report.nodes[found->second];
                    node.status = NodeReportStatus::Completed;
                    node.output = payload.output;
                    return true;
                },
                [&](const NodeRestored &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = report.nodes[found->second];
                    node.agent = payload.agent;
                    node.status = NodeReportStatus::Completed;
                    node.output = payload.output;
                    node.restored_from_checkpoint = payload.checkpoint;
                    return true;
                },
                [&](const CapabilityUsageRecorded &payload) {
                    static_cast<void>(payload.invocation);
                    ++report.usage.records;
                    report.usage.prompt_tokens += payload.prompt_tokens;
                    report.usage.completion_tokens += payload.completion_tokens;
                    report.usage.total_tokens += payload.total_tokens;
                    report.usage.total_cost_usd += payload.total_cost_usd;
                    return true;
                },
                [&](const CapabilityCompleted &payload) {
                    if (payload.cache_hit) {
                        ++report.usage.cache_hits;
                    }
                    return true;
                },
                [&](const ProviderDegraded &) {
                    ++report.usage.degraded_providers;
                    return true;
                },
                [&](const NodeFailed &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = report.nodes[found->second];
                    node.status = NodeReportStatus::Failed;
                    node.diagnostic = payload.diagnostic;
                    node.failure_kind = payload.kind;
                    return true;
                },
                [&](const NodeSkipped &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    report.nodes[found->second].status = NodeReportStatus::Skipped;
                    return true;
                },
                [&](const WorkflowCompleted &payload) {
                    report.output = payload.output;
                    return true;
                },
                [&](const WorkflowFailed &payload) {
                    report.failure_kind = payload.kind;
                    return true;
                },
                [&](const RunCompleted &payload) {
                    report.status = payload.status;
                    saw_run_terminal = true;
                    return true;
                },
                [](const auto &) { return true; },
            },
            event.payload);
        if (!outcome) {
            return std::unexpected(ExecutionReportBuildError{
                .kind = ExecutionReportBuildIssueKind::UnknownNode,
                .event = event.id,
            });
        }
    }

    if (!saw_run || !saw_run_terminal) {
        return missing_required(ExecutionReportBuildIssueKind::MissingRun);
    }
    if (!saw_workflow) {
        return missing_required(ExecutionReportBuildIssueKind::MissingWorkflow);
    }
    return report;
}

} // namespace ahfl::runtime

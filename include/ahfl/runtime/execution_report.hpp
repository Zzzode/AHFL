#pragma once

#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"

namespace ahfl::runtime {

enum class NodeReportStatus {
    Scheduled,
    Running,
    Completed,
    Failed,
    Skipped,
};

struct ExecutionNodeReport {
    WorkflowNodeId node;
    AgentId agent{};
    NodeReportStatus status{NodeReportStatus::Scheduled};
    std::size_t execution_slot{0};
    std::vector<WorkflowNodeId> dependencies;
    std::optional<RuntimeValueId> output{};
    std::optional<DiagnosticId> diagnostic{};
    std::optional<NodeFailureKind> failure_kind{};
    std::optional<CheckpointId> restored_from_checkpoint{};
};

struct ExecutionUsageSummary {
    std::size_t records{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double total_cost_usd{0.0};
    std::size_t cache_hits{0};
    std::size_t degraded_providers{0};
};

struct ExecutionReport {
    RunId run;
    WorkflowId workflow;
    RunTerminalStatus status{RunTerminalStatus::Failed};
    std::vector<WorkflowNodeId> execution_order;
    std::vector<ExecutionNodeReport> nodes;
    std::optional<RuntimeValueId> output;
    std::optional<WorkflowFailureKind> failure_kind;
    ExecutionUsageSummary usage;
};

enum class ExecutionReportBuildIssueKind {
    InvalidEventStream,
    MissingRun,
    MissingWorkflow,
    UnknownNode,
};

struct ExecutionReportBuildError {
    ExecutionReportBuildIssueKind kind{ExecutionReportBuildIssueKind::InvalidEventStream};
    ExecutionEventValidationResult validation{};
    ExecutionEventId event{};
    WorkflowNodeId node{};
};

using ExecutionReportBuildResult = std::expected<ExecutionReport, ExecutionReportBuildError>;

[[nodiscard]] ExecutionReportBuildResult
build_execution_report(std::span<const ExecutionEvent> events);

} // namespace ahfl::runtime

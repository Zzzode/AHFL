#pragma once

#include <expected>
#include <optional>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"

namespace ahfl::runtime {

struct WorkflowResult;

enum class ReplayNodeTerminal {
    Pending,
    Completed,
    Failed,
    Skipped,
};

struct ExecutionReplayNode {
    WorkflowNodeId node;
    AgentId agent{};
    std::size_t execution_slot{0};
    std::vector<WorkflowNodeId> dependencies;
    bool scheduled{false};
    bool started{false};
    ReplayNodeTerminal terminal{ReplayNodeTerminal::Pending};
    std::vector<WorkflowNodeId> blocking_dependencies{};
    std::optional<RuntimeValueId> output{};
    std::optional<DiagnosticId> diagnostic{};
    bool restored{false};
    std::optional<CheckpointId> restored_from_checkpoint{};
};

struct ExecutionReplayProjection {
    RunId run;
    WorkflowId workflow;
    RunTerminalStatus status{RunTerminalStatus::Failed};
    std::vector<WorkflowNodeId> execution_order{};
    std::vector<ExecutionReplayNode> nodes{};
    std::vector<CheckpointId> checkpoints{};
};

struct ExecutionAuditProjection {
    RunId run;
    WorkflowId workflow;
    RunTerminalStatus status{RunTerminalStatus::Failed};
    std::size_t total_events{0};
    std::size_t node_scheduled{0};
    std::size_t node_started{0};
    std::size_t node_completed{0};
    std::size_t node_restored{0};
    std::size_t node_failed{0};
    std::size_t node_skipped{0};
    std::size_t capability_started{0};
    std::size_t capability_usage_recorded{0};
    std::size_t capability_completed{0};
    std::size_t capability_failed{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::size_t total_tokens{0};
    double total_cost_usd{0.0};
    std::size_t cache_hits{0};
    std::size_t provider_degraded{0};
    std::size_t workflow_completed{0};
    std::size_t workflow_failed{0};
    std::size_t checkpoints_saved{0};
    bool terminal_invariant_holds{false};
};

enum class ExecutionSchedulerNodeState {
    Scheduled,
    Running,
    Completed,
    Failed,
    Skipped,
};

enum class ExecutionSchedulerStatus {
    Runnable,
    Waiting,
    TerminalCompleted,
    TerminalFailed,
    TerminalCancelled,
    TerminalInterrupted,
};

struct ExecutionSchedulerNode {
    WorkflowNodeId node;
    AgentId agent{};
    std::size_t execution_slot{0};
    std::vector<WorkflowNodeId> dependencies;
    std::vector<WorkflowNodeId> satisfied_dependencies{};
    std::vector<WorkflowNodeId> blocking_dependencies;
    ExecutionSchedulerNodeState state{ExecutionSchedulerNodeState::Scheduled};
    std::optional<RuntimeValueId> output{};
    std::optional<DiagnosticId> diagnostic{};
    std::optional<CheckpointId> restored_from_checkpoint{};
};

struct ExecutionSchedulerProjection {
    RunId run;
    WorkflowId workflow;
    ExecutionSchedulerStatus status{ExecutionSchedulerStatus::Waiting};
    std::vector<WorkflowNodeId> execution_order{};
    std::vector<ExecutionSchedulerNode> nodes{};
    std::vector<WorkflowNodeId> completed_prefix{};
    std::optional<WorkflowNodeId> next_candidate{};
};

struct ExecutionCheckpointNode {
    WorkflowNodeId node;
    AgentId agent;
    std::optional<RuntimeValueId> output;
};

struct ExecutionCheckpointProjection {
    RunId run;
    WorkflowId workflow;
    CheckpointId checkpoint;
    std::vector<ExecutionCheckpointNode> completed_nodes{};
    std::optional<WorkflowNodeId> resume_candidate{};
    bool resume_ready{false};
};

enum class ExecutionProjectionError {
    InvalidEventStream,
    UnknownNode,
    MissingCheckpoint,
};

using ExecutionReplayProjectionResult =
    std::expected<ExecutionReplayProjection, ExecutionProjectionError>;
using ExecutionAuditProjectionResult =
    std::expected<ExecutionAuditProjection, ExecutionProjectionError>;
using ExecutionSchedulerProjectionResult =
    std::expected<ExecutionSchedulerProjection, ExecutionProjectionError>;
using ExecutionCheckpointProjectionResult =
    std::expected<ExecutionCheckpointProjection, ExecutionProjectionError>;

[[nodiscard]] ExecutionReplayProjectionResult
build_execution_replay_projection(const WorkflowResult &result);

[[nodiscard]] ExecutionAuditProjectionResult
build_execution_audit_projection(const WorkflowResult &result);

[[nodiscard]] ExecutionSchedulerProjectionResult
build_execution_scheduler_projection(const WorkflowResult &result);

[[nodiscard]] ExecutionCheckpointProjectionResult
build_execution_checkpoint_projection(const WorkflowResult &result);

} // namespace ahfl::runtime

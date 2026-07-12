#include "ahfl/runtime/execution_projection.hpp"

#include <map>
#include <set>

#include "ahfl/base/support/overloaded.hpp"
#include "runtime/engine/workflow_runtime.hpp"

namespace ahfl::runtime {

ExecutionReplayProjectionResult
build_execution_replay_projection(const WorkflowResult &result) {
    const auto validation = validate_execution_events(result.events.events());
    if (!validation.ok()) {
        return std::unexpected(ExecutionProjectionError::InvalidEventStream);
    }

    ExecutionReplayProjection projection{
        .run = result.report.run,
        .workflow = result.report.workflow,
        .status = result.report.status,
    };
    std::map<WorkflowNodeId, std::size_t> node_indices;

    for (const auto &event : result.events.events()) {
        const auto ok = std::visit(
            ahfl::Overloaded{
                [&](const NodeScheduled &payload) {
                    node_indices.emplace(payload.node, projection.nodes.size());
                    projection.execution_order.push_back(payload.node);
                    projection.nodes.push_back(ExecutionReplayNode{
                        .node = payload.node,
                        .execution_slot = payload.execution_slot,
                        .dependencies = payload.dependencies,
                        .scheduled = true,
                    });
                    return true;
                },
                [&](const NodeStarted &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    projection.nodes[found->second].agent = payload.agent;
                    projection.nodes[found->second].started = true;
                    return true;
                },
                [&](const NodeCompleted &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = projection.nodes[found->second];
                    node.terminal = ReplayNodeTerminal::Completed;
                    node.output = payload.output;
                    return true;
                },
                [&](const NodeRestored &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = projection.nodes[found->second];
                    node.agent = payload.agent;
                    node.terminal = ReplayNodeTerminal::Completed;
                    node.output = payload.output;
                    node.restored = true;
                    node.restored_from_checkpoint = payload.checkpoint;
                    return true;
                },
                [&](const NodeFailed &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = projection.nodes[found->second];
                    node.terminal = ReplayNodeTerminal::Failed;
                    node.diagnostic = payload.diagnostic;
                    return true;
                },
                [&](const NodeSkipped &payload) {
                    const auto found = node_indices.find(payload.node);
                    if (found == node_indices.end()) {
                        return false;
                    }
                    auto &node = projection.nodes[found->second];
                    node.terminal = ReplayNodeTerminal::Skipped;
                    node.blocking_dependencies = payload.blocking_dependencies;
                    return true;
                },
                [&](const CheckpointSaved &payload) {
                    projection.checkpoints.push_back(payload.checkpoint);
                    return true;
                },
                [](const auto &) { return true; },
            },
            event.payload);
        if (!ok) {
            return std::unexpected(ExecutionProjectionError::UnknownNode);
        }
    }
    return projection;
}

ExecutionAuditProjectionResult
build_execution_audit_projection(const WorkflowResult &result) {
    const auto validation = validate_execution_events(result.events.events());
    if (!validation.ok()) {
        return std::unexpected(ExecutionProjectionError::InvalidEventStream);
    }

    ExecutionAuditProjection projection{
        .run = result.report.run,
        .workflow = result.report.workflow,
        .status = result.report.status,
        .total_events = result.events.size(),
        .terminal_invariant_holds = true,
    };
    for (const auto &event : result.events.events()) {
        std::visit(
            ahfl::Overloaded{
                [&](const NodeScheduled &) { ++projection.node_scheduled; },
                [&](const NodeStarted &) { ++projection.node_started; },
                [&](const NodeCompleted &) { ++projection.node_completed; },
                [&](const NodeRestored &) { ++projection.node_restored; },
                [&](const NodeFailed &) { ++projection.node_failed; },
                [&](const NodeSkipped &) { ++projection.node_skipped; },
                [&](const CapabilityStarted &) { ++projection.capability_started; },
                [&](const CapabilityUsageRecorded &payload) {
                    ++projection.capability_usage_recorded;
                    projection.prompt_tokens += payload.prompt_tokens;
                    projection.completion_tokens += payload.completion_tokens;
                    projection.total_tokens += payload.total_tokens;
                    projection.total_cost_usd += payload.total_cost_usd;
                },
                [&](const CapabilityCompleted &payload) {
                    ++projection.capability_completed;
                    if (payload.cache_hit) {
                        ++projection.cache_hits;
                    }
                },
                [&](const CapabilityFailed &) { ++projection.capability_failed; },
                [&](const ProviderDegraded &) { ++projection.provider_degraded; },
                [&](const WorkflowCompleted &) { ++projection.workflow_completed; },
                [&](const WorkflowFailed &) { ++projection.workflow_failed; },
                [&](const CheckpointSaved &) { ++projection.checkpoints_saved; },
                [](const auto &) {},
            },
            event.payload);
    }
    return projection;
}

ExecutionSchedulerProjectionResult
build_execution_scheduler_projection(const WorkflowResult &result) {
    const auto replay = build_execution_replay_projection(result);
    if (!replay.has_value()) {
        return std::unexpected(replay.error());
    }

    ExecutionSchedulerProjection projection{
        .run = replay->run,
        .workflow = replay->workflow,
        .execution_order = replay->execution_order,
    };
    switch (replay->status) {
    case RunTerminalStatus::Completed:
        projection.status = ExecutionSchedulerStatus::TerminalCompleted;
        break;
    case RunTerminalStatus::Failed:
        projection.status = ExecutionSchedulerStatus::TerminalFailed;
        break;
    case RunTerminalStatus::Cancelled:
        projection.status = ExecutionSchedulerStatus::TerminalCancelled;
        break;
    case RunTerminalStatus::Interrupted:
        projection.status = ExecutionSchedulerStatus::TerminalInterrupted;
        break;
    }

    std::set<WorkflowNodeId> completed;
    for (const auto &node : replay->nodes) {
        if (node.terminal == ReplayNodeTerminal::Completed) {
            completed.insert(node.node);
        }
    }

    projection.nodes.reserve(replay->nodes.size());
    for (const auto &node : replay->nodes) {
        ExecutionSchedulerNode projected{
            .node = node.node,
            .agent = node.agent,
            .execution_slot = node.execution_slot,
            .dependencies = node.dependencies,
            .blocking_dependencies = node.blocking_dependencies,
            .output = node.output,
            .diagnostic = node.diagnostic,
            .restored_from_checkpoint = node.restored_from_checkpoint,
        };
        for (const auto dependency : node.dependencies) {
            if (completed.contains(dependency)) {
                projected.satisfied_dependencies.push_back(dependency);
            }
        }
        switch (node.terminal) {
        case ReplayNodeTerminal::Completed:
            projected.state = ExecutionSchedulerNodeState::Completed;
            break;
        case ReplayNodeTerminal::Failed:
            projected.state = ExecutionSchedulerNodeState::Failed;
            break;
        case ReplayNodeTerminal::Skipped:
            projected.state = ExecutionSchedulerNodeState::Skipped;
            break;
        case ReplayNodeTerminal::Pending:
            projected.state = node.started ? ExecutionSchedulerNodeState::Running
                                           : ExecutionSchedulerNodeState::Scheduled;
            break;
        }
        projection.nodes.push_back(std::move(projected));
    }

    for (const auto node : projection.execution_order) {
        if (completed.contains(node)) {
            projection.completed_prefix.push_back(node);
            continue;
        }
        projection.next_candidate = node;
        break;
    }
    return projection;
}

ExecutionCheckpointProjectionResult
build_execution_checkpoint_projection(const WorkflowResult &result) {
    const auto validation = validate_execution_events(result.events.events());
    if (!validation.ok()) {
        return std::unexpected(ExecutionProjectionError::InvalidEventStream);
    }

    struct NodeState {
        AgentId agent;
        std::optional<RuntimeValueId> output;
        bool completed{false};
    };

    std::map<WorkflowNodeId, NodeState> node_states;
    std::vector<WorkflowNodeId> execution_order;
    std::optional<ExecutionCheckpointProjection> latest;
    RunId run;
    WorkflowId workflow;

    for (const auto &event : result.events.events()) {
        const auto ok = std::visit(
            ahfl::Overloaded{
                [&](const RunStarted &payload) {
                    run = payload.run;
                    return true;
                },
                [&](const WorkflowStarted &payload) {
                    workflow = payload.workflow;
                    return true;
                },
                [&](const NodeScheduled &payload) {
                    node_states.emplace(payload.node, NodeState{});
                    execution_order.push_back(payload.node);
                    return true;
                },
                [&](const NodeStarted &payload) {
                    const auto found = node_states.find(payload.node);
                    if (found == node_states.end()) {
                        return false;
                    }
                    found->second.agent = payload.agent;
                    return true;
                },
                [&](const NodeCompleted &payload) {
                    const auto found = node_states.find(payload.node);
                    if (found == node_states.end()) {
                        return false;
                    }
                    found->second.output = payload.output;
                    found->second.completed = true;
                    return true;
                },
                [&](const NodeRestored &payload) {
                    const auto found = node_states.find(payload.node);
                    if (found == node_states.end()) {
                        return false;
                    }
                    found->second.agent = payload.agent;
                    found->second.output = payload.output;
                    found->second.completed = true;
                    return true;
                },
                [&](const CheckpointSaved &payload) {
                    ExecutionCheckpointProjection checkpoint{
                        .run = payload.run,
                        .workflow = workflow,
                        .checkpoint = payload.checkpoint,
                    };
                    for (const auto node : execution_order) {
                        const auto found = node_states.find(node);
                        if (found == node_states.end()) {
                            return false;
                        }
                        if (!found->second.completed) {
                            if (!checkpoint.resume_candidate.has_value()) {
                                checkpoint.resume_candidate = node;
                            }
                            continue;
                        }
                        checkpoint.completed_nodes.push_back(ExecutionCheckpointNode{
                            .node = node,
                            .agent = found->second.agent,
                            .output = found->second.output,
                        });
                    }
                    checkpoint.resume_ready = checkpoint.resume_candidate.has_value();
                    latest = std::move(checkpoint);
                    return true;
                },
                [](const auto &) { return true; },
            },
            event.payload);
        if (!ok) {
            return std::unexpected(ExecutionProjectionError::UnknownNode);
        }
    }

    if (!latest.has_value()) {
        return std::unexpected(ExecutionProjectionError::MissingCheckpoint);
    }
    if (latest->run != run) {
        return std::unexpected(ExecutionProjectionError::InvalidEventStream);
    }
    return std::move(*latest);
}

} // namespace ahfl::runtime

#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::scheduler_snapshot {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.SCHEDULER_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "scheduler decision summary");
}

[[nodiscard]] std::optional<std::string>
terminal_reason_for_snapshot(const SchedulerSnapshot &snapshot) {
    switch (snapshot.snapshot_status) {
    case SchedulerSnapshotStatus::Runnable:
    case SchedulerSnapshotStatus::Waiting:
        return std::nullopt;
    case SchedulerSnapshotStatus::TerminalCompleted:
        return std::string("workflow_completed");
    case SchedulerSnapshotStatus::TerminalFailed:
        return std::string("workflow_failed");
    case SchedulerSnapshotStatus::TerminalPartial:
        return std::string("partial_snapshot_retained");
    }

    return std::nullopt;
}

[[nodiscard]] std::string next_step_recommendation_for_snapshot(const SchedulerSnapshot &snapshot) {
    switch (snapshot.snapshot_status) {
    case SchedulerSnapshotStatus::Runnable:
        if (snapshot.cursor.next_candidate_node_name.has_value()) {
            return "run ready node '" + *snapshot.cursor.next_candidate_node_name + "'";
        }
        return "run next ready node from ready set";
    case SchedulerSnapshotStatus::Waiting:
        if (snapshot.cursor.next_candidate_node_name.has_value()) {
            return "wait for blocked node '" + *snapshot.cursor.next_candidate_node_name +
                   "' to become ready";
        }
        return "wait for blocked frontier to advance";
    case SchedulerSnapshotStatus::TerminalCompleted:
        return "workflow is complete; no further scheduler action";
    case SchedulerSnapshotStatus::TerminalFailed:
        if (snapshot.workflow_failure_summary.has_value() &&
            snapshot.workflow_failure_summary->node_name.has_value()) {
            return "inspect workflow failure at node '" +
                   *snapshot.workflow_failure_summary->node_name +
                   "'; do not continue scheduling";
        }
        return "inspect workflow failure; do not continue scheduling";
    case SchedulerSnapshotStatus::TerminalPartial:
        return "preserve partial snapshot; no further scheduler action";
    }

    return "no scheduler action";
}

[[nodiscard]] SchedulerNextActionKind next_action_for_snapshot(const SchedulerSnapshot &snapshot) {
    switch (snapshot.snapshot_status) {
    case SchedulerSnapshotStatus::Runnable:
        return SchedulerNextActionKind::RunReadyNode;
    case SchedulerSnapshotStatus::Waiting:
        return SchedulerNextActionKind::AwaitDependencies;
    case SchedulerSnapshotStatus::TerminalCompleted:
        return SchedulerNextActionKind::WorkflowCompleted;
    case SchedulerSnapshotStatus::TerminalFailed:
        return SchedulerNextActionKind::InvestigateFailure;
    case SchedulerSnapshotStatus::TerminalPartial:
        return SchedulerNextActionKind::PreservePartialState;
    }

    return SchedulerNextActionKind::AwaitDependencies;
}

} // namespace

SchedulerDecisionSummaryValidationResult
validate_scheduler_decision_summary(const SchedulerDecisionSummary &summary) {
    SchedulerDecisionSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kSchedulerDecisionSummaryFormatVersion) {
        emit_validation_error(diagnostics, "scheduler decision summary format_version must be '" +
                          std::string(kSchedulerDecisionSummaryFormatVersion) + "'");
    }

    if (summary.source_scheduler_snapshot_format_version != kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics, 
            "scheduler decision summary source_scheduler_snapshot_format_version must be '" +
            std::string(kSchedulerSnapshotFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary workflow_canonical_name must not be empty");
    }

    if (summary.session_id.empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary run_id must not be empty when present");
    }

    if (summary.input_fixture.empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary input_fixture must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(
            *summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.completed_prefix_size != summary.completed_prefix.size()) {
        emit_validation_error(diagnostics, 
            "scheduler decision summary completed_prefix_size must match completed_prefix length");
    }

    if (summary.next_candidate_node_name.has_value() &&
        summary.next_candidate_node_name->empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary next_candidate_node_name must not be empty");
    }

    if (summary.terminal_reason.has_value() && summary.terminal_reason->empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary terminal_reason must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics, "scheduler decision summary next_step_recommendation must not be empty");
    }

    switch (summary.snapshot_status) {
    case SchedulerSnapshotStatus::Runnable:
        if (summary.next_action != SchedulerNextActionKind::RunReadyNode) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary runnable snapshot_status requires next_action run_ready_node");
        }
        if (summary.terminal_reason.has_value()) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary runnable snapshot_status must not declare terminal_reason");
        }
        break;
    case SchedulerSnapshotStatus::Waiting:
        if (summary.next_action != SchedulerNextActionKind::AwaitDependencies) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary waiting snapshot_status requires next_action await_dependencies");
        }
        if (summary.terminal_reason.has_value()) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary waiting snapshot_status must not declare terminal_reason");
        }
        break;
    case SchedulerSnapshotStatus::TerminalCompleted:
        if (summary.next_action != SchedulerNextActionKind::WorkflowCompleted) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal completed snapshot_status requires next_action workflow_completed");
        }
        if (summary.terminal_reason != "workflow_completed") {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal completed snapshot_status requires terminal_reason 'workflow_completed'");
        }
        break;
    case SchedulerSnapshotStatus::TerminalFailed:
        if (summary.next_action != SchedulerNextActionKind::InvestigateFailure) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal failed snapshot_status requires next_action investigate_failure");
        }
        if (summary.terminal_reason != "workflow_failed") {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal failed snapshot_status requires terminal_reason 'workflow_failed'");
        }
        break;
    case SchedulerSnapshotStatus::TerminalPartial:
        if (summary.next_action != SchedulerNextActionKind::PreservePartialState) {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal partial snapshot_status requires next_action preserve_partial_state");
        }
        if (summary.terminal_reason != "partial_snapshot_retained") {
            emit_validation_error(diagnostics, 
                "scheduler decision summary terminal partial snapshot_status requires terminal_reason 'partial_snapshot_retained'");
        }
        break;
    }

    return result;
}

SchedulerDecisionSummaryResult
build_scheduler_decision_summary(const SchedulerSnapshot &snapshot) {
    SchedulerDecisionSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_scheduler_snapshot(snapshot);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    SchedulerDecisionSummary summary{
        .format_version = std::string(kSchedulerDecisionSummaryFormatVersion),
        .source_scheduler_snapshot_format_version = snapshot.format_version,
        .workflow_canonical_name = snapshot.workflow_canonical_name,
        .session_id = snapshot.session_id,
        .run_id = snapshot.run_id,
        .input_fixture = snapshot.input_fixture,
        .workflow_status = snapshot.workflow_status,
        .snapshot_status = snapshot.snapshot_status,
        .workflow_failure_summary = snapshot.workflow_failure_summary,
        .completed_prefix_size = snapshot.cursor.completed_prefix_size,
        .completed_prefix = snapshot.cursor.completed_prefix,
        .next_candidate_node_name = snapshot.cursor.next_candidate_node_name,
        .checkpoint_friendly = snapshot.cursor.checkpoint_friendly,
        .ready_nodes = {},
        .blocked_nodes = {},
        .terminal_reason = terminal_reason_for_snapshot(snapshot),
        .next_action = next_action_for_snapshot(snapshot),
        .next_step_recommendation = next_step_recommendation_for_snapshot(snapshot),
    };

    summary.ready_nodes.reserve(snapshot.ready_nodes.size());
    for (const auto &node : snapshot.ready_nodes) {
        summary.ready_nodes.push_back(SchedulerReviewReadyNode{
            .node_name = node.node_name,
            .target = node.target,
            .execution_index = node.execution_index,
            .planned_dependencies = node.planned_dependencies,
            .satisfied_dependencies = node.satisfied_dependencies,
        });
    }

    summary.blocked_nodes.reserve(snapshot.blocked_nodes.size());
    for (const auto &node : snapshot.blocked_nodes) {
        summary.blocked_nodes.push_back(SchedulerReviewBlockedNode{
            .node_name = node.node_name,
            .target = node.target,
            .execution_index = node.execution_index,
            .blocked_reason = node.blocked_reason,
            .missing_dependencies = node.missing_dependencies,
            .may_become_ready = node.may_become_ready,
        });
    }

    const auto validation_result = validate_scheduler_decision_summary(summary);
    result.diagnostics.append(validation_result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::scheduler_snapshot

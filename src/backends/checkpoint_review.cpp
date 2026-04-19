#include "ahfl/backends/checkpoint_review.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string snapshot_status_name(
    scheduler_snapshot::SchedulerSnapshotStatus status) {
    switch (status) {
    case scheduler_snapshot::SchedulerSnapshotStatus::Runnable:
        return "runnable";
    case scheduler_snapshot::SchedulerSnapshotStatus::Waiting:
        return "waiting";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted:
        return "terminal_completed";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed:
        return "terminal_failed";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(
    checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string basis_kind_name(checkpoint_record::CheckpointBasisKind kind) {
    switch (kind) {
    case checkpoint_record::CheckpointBasisKind::LocalOnly:
        return "local_only";
    case checkpoint_record::CheckpointBasisKind::DurableAdjacent:
        return "durable_adjacent";
    }

    return "invalid";
}

[[nodiscard]] std::string resume_blocker_kind_name(
    checkpoint_record::CheckpointResumeBlockerKind kind) {
    switch (kind) {
    case checkpoint_record::CheckpointResumeBlockerKind::NotCheckpointFriendly:
        return "not_checkpoint_friendly";
    case checkpoint_record::CheckpointResumeBlockerKind::WaitingOnSchedulerState:
        return "waiting_on_scheduler_state";
    case checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case checkpoint_record::CheckpointResumeBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(
    checkpoint_record::CheckpointReviewNextActionKind action) {
    switch (action) {
    case checkpoint_record::CheckpointReviewNextActionKind::PersistCheckpoint:
        return "persist_checkpoint";
    case checkpoint_record::CheckpointReviewNextActionKind::AwaitCheckpointReadiness:
        return "await_checkpoint_readiness";
    case checkpoint_record::CheckpointReviewNextActionKind::WorkflowCompleted:
        return "workflow_completed";
    case checkpoint_record::CheckpointReviewNextActionKind::InvestigateFailure:
        return "investigate_failure";
    case checkpoint_record::CheckpointReviewNextActionKind::PreservePartialState:
        return "preserve_partial_state";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_resume_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<checkpoint_record::CheckpointResumeBlocker> &blocker) {
    line(out, indent_level, "resume_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + resume_blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "node_name " + (blocker->node_name.has_value() ? *blocker->node_name : "none"));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

void print_string_list(std::ostream &out,
                       int indent_level,
                       std::string_view label,
                       const std::vector<std::string> &values) {
    line(out, indent_level, std::string(label) + " {");
    line(out, indent_level + 1, "count " + std::to_string(values.size()));
    if (values.empty()) {
        line(out, indent_level + 1, "- none");
    } else {
        for (const auto &value : values) {
            line(out, indent_level + 1, "- " + value);
        }
    }
    line(out, indent_level, "}");
}

} // namespace

void print_checkpoint_review(const checkpoint_record::CheckpointReviewSummary &summary,
                             std::ostream &out) {
    out << summary.format_version << '\n';
    line(out, 0, "source_record_format " + summary.source_checkpoint_record_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "snapshot_status " + snapshot_status_name(summary.snapshot_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "basis_kind " + basis_kind_name(summary.basis_kind));
    line(out,
         0,
         std::string("checkpoint_friendly_source ") +
             (summary.checkpoint_friendly_source ? "true" : "false"));
    line(out, 0, std::string("resume_ready ") + (summary.resume_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "checkpoint_boundary " + summary.checkpoint_boundary_summary);
    line(out, 0, "resume_preview " + summary.resume_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "resume_candidate " +
             (summary.resume_candidate_node_name.has_value() ? *summary.resume_candidate_node_name
                                                            : "none"));
    line(out,
         0,
         "terminal_reason " +
             (summary.terminal_reason.has_value() ? *summary.terminal_reason : "none"));

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_resume_blocker(out, 0, summary.resume_blocker);
    print_string_list(out, 0, "persistable_prefix", summary.persistable_prefix);
}

} // namespace ahfl

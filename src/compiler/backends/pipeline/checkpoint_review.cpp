#include "compiler/backends/pipeline/checkpoint_review.hpp"
#include "compiler/backends/pipeline/review_helpers.hpp"
#include "printer_helpers.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

using backend_printer::line;

[[nodiscard]] std::string basis_kind_name(checkpoint_record::CheckpointBasisKind kind) {
    switch (kind) {
    case checkpoint_record::CheckpointBasisKind::LocalOnly:
        return "local_only";
    case checkpoint_record::CheckpointBasisKind::DurableAdjacent:
        return "durable_adjacent";
    }

    return "invalid";
}

[[nodiscard]] std::string
resume_blocker_kind_name(checkpoint_record::CheckpointResumeBlockerKind kind) {
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

[[nodiscard]] std::string
next_action_name(checkpoint_record::CheckpointReviewNextActionKind action) {
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

} // namespace

void print_checkpoint_review(const checkpoint_record::CheckpointReviewSummary &summary,
                             std::ostream &out) {
    out << summary.format_version << '\n';
    line(out, 0, "source_record_format " + summary.source_checkpoint_record_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + pipeline_review::workflow_status_name(summary.workflow_status));
    line(out, 0, "snapshot_status " + pipeline_review::snapshot_status_name(summary.snapshot_status));
    line(out, 0, "checkpoint_status " + pipeline_review::checkpoint_status_name(summary.checkpoint_status));
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
         "resume_candidate " + (summary.resume_candidate_node_name.has_value()
                                    ? *summary.resume_candidate_node_name
                                    : "none"));
    line(out,
         0,
         "terminal_reason " +
             (summary.terminal_reason.has_value() ? *summary.terminal_reason : "none"));

    pipeline_review::print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_resume_blocker(out, 0, summary.resume_blocker);
    pipeline_review::print_string_list(out, 0, "persistable_prefix", summary.persistable_prefix);
}

} // namespace ahfl

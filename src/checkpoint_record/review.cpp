#include "ahfl/checkpoint_record/review.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ahfl::checkpoint_record {

namespace {

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error("checkpoint review summary " + std::string(owner_name) +
                          " contains failure summary with empty message");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error("checkpoint review summary " + std::string(owner_name) +
                          " contains failure summary with empty node_name");
    }
}

void validate_resume_blocker(const CheckpointResumeBlocker &blocker,
                             DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        diagnostics.error("checkpoint review summary resume_blocker message must not be empty");
    }

    if (blocker.node_name.has_value() && blocker.node_name->empty()) {
        diagnostics.error("checkpoint review summary resume_blocker node_name must not be empty");
    }
}

[[nodiscard]] std::optional<std::string>
terminal_reason_for_record(const CheckpointRecord &record) {
    switch (record.checkpoint_status) {
    case CheckpointRecordStatus::ReadyToPersist:
    case CheckpointRecordStatus::Blocked:
        return std::nullopt;
    case CheckpointRecordStatus::TerminalCompleted:
        return std::string("workflow_completed");
    case CheckpointRecordStatus::TerminalFailed:
        return std::string("workflow_failed");
    case CheckpointRecordStatus::TerminalPartial:
        return std::string("partial_checkpoint_retained");
    }

    return std::nullopt;
}

[[nodiscard]] CheckpointReviewNextActionKind
next_action_for_record(const CheckpointRecord &record) {
    switch (record.checkpoint_status) {
    case CheckpointRecordStatus::ReadyToPersist:
        return CheckpointReviewNextActionKind::PersistCheckpoint;
    case CheckpointRecordStatus::Blocked:
        return CheckpointReviewNextActionKind::AwaitCheckpointReadiness;
    case CheckpointRecordStatus::TerminalCompleted:
        return CheckpointReviewNextActionKind::WorkflowCompleted;
    case CheckpointRecordStatus::TerminalFailed:
        return CheckpointReviewNextActionKind::InvestigateFailure;
    case CheckpointRecordStatus::TerminalPartial:
        return CheckpointReviewNextActionKind::PreservePartialState;
    }

    return CheckpointReviewNextActionKind::AwaitCheckpointReadiness;
}

[[nodiscard]] std::string checkpoint_boundary_summary_for_record(const CheckpointRecord &record) {
    switch (record.basis_kind) {
    case CheckpointBasisKind::LocalOnly:
        return "local checkpoint basis only; durable recovery ABI not yet promised";
    case CheckpointBasisKind::DurableAdjacent:
        return "durable-adjacent checkpoint shape available; durable recovery ABI still not promised";
    }

    return "local checkpoint basis only; durable recovery ABI not yet promised";
}

[[nodiscard]] std::string resume_preview_for_record(const CheckpointRecord &record) {
    switch (record.checkpoint_status) {
    case CheckpointRecordStatus::ReadyToPersist:
        if (record.cursor.resume_candidate_node_name.has_value()) {
            return "checkpoint can resume from node '" +
                   *record.cursor.resume_candidate_node_name +
                   "' after persisting current prefix";
        }
        return "checkpoint can resume after persisting current prefix";
    case CheckpointRecordStatus::Blocked:
        if (record.resume_blocker.has_value() &&
            record.resume_blocker->kind ==
                CheckpointResumeBlockerKind::NotCheckpointFriendly) {
            return "resume is blocked because current checkpoint source is not checkpoint friendly";
        }
        if (record.resume_blocker.has_value() && record.resume_blocker->node_name.has_value()) {
            return "resume is blocked while waiting on node '" +
                   *record.resume_blocker->node_name + "'";
        }
        return "resume is currently blocked until checkpoint readiness improves";
    case CheckpointRecordStatus::TerminalCompleted:
        return "workflow already completed; no resume is required";
    case CheckpointRecordStatus::TerminalFailed:
        if (record.workflow_failure_summary.has_value() &&
            record.workflow_failure_summary->node_name.has_value()) {
            return "workflow failed at node '" +
                   *record.workflow_failure_summary->node_name +
                   "'; resume is not available from current checkpoint";
        }
        return "workflow failed; resume is not available from current checkpoint";
    case CheckpointRecordStatus::TerminalPartial:
        return "partial checkpoint is retained, but current state is not resumable";
    }

    return "resume state unavailable";
}

[[nodiscard]] std::string next_step_recommendation_for_record(const CheckpointRecord &record) {
    switch (record.checkpoint_status) {
    case CheckpointRecordStatus::ReadyToPersist:
        return "persist current checkpoint basis before future resume";
    case CheckpointRecordStatus::Blocked:
        if (record.resume_blocker.has_value() &&
            record.resume_blocker->kind ==
                CheckpointResumeBlockerKind::NotCheckpointFriendly) {
            return "keep checkpoint local-only and avoid advertising durable resume";
        }
        if (record.resume_blocker.has_value() && record.resume_blocker->node_name.has_value()) {
            return "wait until node '" + *record.resume_blocker->node_name +
                   "' becomes checkpoint-resumable";
        }
        return "wait until checkpoint readiness blockers clear";
    case CheckpointRecordStatus::TerminalCompleted:
        return "archive completed checkpoint basis; no further resume action";
    case CheckpointRecordStatus::TerminalFailed:
        return "inspect workflow failure before attempting a new run";
    case CheckpointRecordStatus::TerminalPartial:
        return "preserve partial checkpoint for inspection; do not advertise resume";
    }

    return "no checkpoint action";
}

} // namespace

CheckpointReviewSummaryValidationResult
validate_checkpoint_review_summary(const CheckpointReviewSummary &summary) {
    CheckpointReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kCheckpointReviewSummaryFormatVersion) {
        diagnostics.error("checkpoint review summary format_version must be '" +
                          std::string(kCheckpointReviewSummaryFormatVersion) + "'");
    }

    if (summary.source_checkpoint_record_format_version != kCheckpointRecordFormatVersion) {
        diagnostics.error("checkpoint review summary source_checkpoint_record_format_version must be '" +
                          std::string(kCheckpointRecordFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        diagnostics.error("checkpoint review summary workflow_canonical_name must not be empty");
    }

    if (summary.session_id.empty()) {
        diagnostics.error("checkpoint review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        diagnostics.error("checkpoint review summary run_id must not be empty when present");
    }

    if (summary.input_fixture.empty()) {
        diagnostics.error("checkpoint review summary input_fixture must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.persistable_prefix_size != summary.persistable_prefix.size()) {
        diagnostics.error(
            "checkpoint review summary persistable_prefix_size must match persistable_prefix length");
    }

    if (summary.resume_candidate_node_name.has_value() &&
        summary.resume_candidate_node_name->empty()) {
        diagnostics.error(
            "checkpoint review summary resume_candidate_node_name must not be empty");
    }

    if (summary.resume_blocker.has_value()) {
        validate_resume_blocker(*summary.resume_blocker, diagnostics);
    }

    if (summary.resume_ready && summary.resume_blocker.has_value()) {
        diagnostics.error(
            "checkpoint review summary cannot contain resume_blocker when resume_ready is true");
    }

    if (!summary.terminal_reason.has_value() &&
        (summary.checkpoint_status == CheckpointRecordStatus::TerminalCompleted ||
         summary.checkpoint_status == CheckpointRecordStatus::TerminalFailed ||
         summary.checkpoint_status == CheckpointRecordStatus::TerminalPartial)) {
        diagnostics.error(
            "checkpoint review summary terminal checkpoint_status requires terminal_reason");
    }

    if (summary.terminal_reason.has_value() && summary.terminal_reason->empty()) {
        diagnostics.error("checkpoint review summary terminal_reason must not be empty");
    }

    if (summary.checkpoint_boundary_summary.empty()) {
        diagnostics.error(
            "checkpoint review summary checkpoint_boundary_summary must not be empty");
    }

    if (summary.resume_preview.empty()) {
        diagnostics.error("checkpoint review summary resume_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        diagnostics.error(
            "checkpoint review summary next_step_recommendation must not be empty");
    }

    switch (summary.checkpoint_status) {
    case CheckpointRecordStatus::ReadyToPersist:
        if (summary.next_action != CheckpointReviewNextActionKind::PersistCheckpoint) {
            diagnostics.error(
                "checkpoint review summary ReadyToPersist checkpoint_status requires next_action persist_checkpoint");
        }
        if (summary.terminal_reason.has_value()) {
            diagnostics.error(
                "checkpoint review summary ReadyToPersist checkpoint_status must not declare terminal_reason");
        }
        if (!summary.resume_ready) {
            diagnostics.error(
                "checkpoint review summary ReadyToPersist checkpoint_status requires resume_ready");
        }
        break;
    case CheckpointRecordStatus::Blocked:
        if (summary.next_action != CheckpointReviewNextActionKind::AwaitCheckpointReadiness) {
            diagnostics.error(
                "checkpoint review summary Blocked checkpoint_status requires next_action await_checkpoint_readiness");
        }
        if (summary.terminal_reason.has_value()) {
            diagnostics.error(
                "checkpoint review summary Blocked checkpoint_status must not declare terminal_reason");
        }
        if (summary.resume_ready) {
            diagnostics.error(
                "checkpoint review summary Blocked checkpoint_status cannot be resume_ready");
        }
        if (!summary.resume_blocker.has_value()) {
            diagnostics.error(
                "checkpoint review summary Blocked checkpoint_status requires resume_blocker");
        }
        break;
    case CheckpointRecordStatus::TerminalCompleted:
        if (summary.next_action != CheckpointReviewNextActionKind::WorkflowCompleted) {
            diagnostics.error(
                "checkpoint review summary TerminalCompleted checkpoint_status requires next_action workflow_completed");
        }
        if (summary.terminal_reason != "workflow_completed") {
            diagnostics.error(
                "checkpoint review summary TerminalCompleted checkpoint_status requires terminal_reason 'workflow_completed'");
        }
        break;
    case CheckpointRecordStatus::TerminalFailed:
        if (summary.next_action != CheckpointReviewNextActionKind::InvestigateFailure) {
            diagnostics.error(
                "checkpoint review summary TerminalFailed checkpoint_status requires next_action investigate_failure");
        }
        if (summary.terminal_reason != "workflow_failed") {
            diagnostics.error(
                "checkpoint review summary TerminalFailed checkpoint_status requires terminal_reason 'workflow_failed'");
        }
        if (!summary.resume_blocker.has_value()) {
            diagnostics.error(
                "checkpoint review summary TerminalFailed checkpoint_status requires resume_blocker");
        }
        break;
    case CheckpointRecordStatus::TerminalPartial:
        if (summary.next_action != CheckpointReviewNextActionKind::PreservePartialState) {
            diagnostics.error(
                "checkpoint review summary TerminalPartial checkpoint_status requires next_action preserve_partial_state");
        }
        if (summary.terminal_reason != "partial_checkpoint_retained") {
            diagnostics.error(
                "checkpoint review summary TerminalPartial checkpoint_status requires terminal_reason 'partial_checkpoint_retained'");
        }
        if (!summary.resume_blocker.has_value()) {
            diagnostics.error(
                "checkpoint review summary TerminalPartial checkpoint_status requires resume_blocker");
        }
        break;
    }

    return result;
}

CheckpointReviewSummaryResult
build_checkpoint_review_summary(const CheckpointRecord &record) {
    CheckpointReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_checkpoint_record(record);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    CheckpointReviewSummary summary{
        .format_version = std::string(kCheckpointReviewSummaryFormatVersion),
        .source_checkpoint_record_format_version = record.format_version,
        .workflow_canonical_name = record.workflow_canonical_name,
        .session_id = record.session_id,
        .run_id = record.run_id,
        .input_fixture = record.input_fixture,
        .workflow_status = record.workflow_status,
        .snapshot_status = record.snapshot_status,
        .checkpoint_status = record.checkpoint_status,
        .workflow_failure_summary = record.workflow_failure_summary,
        .basis_kind = record.basis_kind,
        .checkpoint_friendly_source = record.checkpoint_friendly_source,
        .persistable_prefix_size = record.cursor.persistable_prefix_size,
        .persistable_prefix = record.cursor.persistable_prefix,
        .resume_candidate_node_name = record.cursor.resume_candidate_node_name,
        .resume_ready = record.cursor.resume_ready,
        .resume_blocker = record.resume_blocker,
        .terminal_reason = terminal_reason_for_record(record),
        .next_action = next_action_for_record(record),
        .checkpoint_boundary_summary = checkpoint_boundary_summary_for_record(record),
        .resume_preview = resume_preview_for_record(record),
        .next_step_recommendation = next_step_recommendation_for_record(record),
    };

    const auto validation_result = validate_checkpoint_review_summary(summary);
    result.diagnostics.append(validation_result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::checkpoint_record

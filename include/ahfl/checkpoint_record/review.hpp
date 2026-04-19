#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/checkpoint_record/record.hpp"

namespace ahfl::checkpoint_record {

inline constexpr std::string_view kCheckpointReviewSummaryFormatVersion =
    "ahfl.checkpoint-review.v1";

enum class CheckpointReviewNextActionKind {
    PersistCheckpoint,
    AwaitCheckpointReadiness,
    WorkflowCompleted,
    InvestigateFailure,
    PreservePartialState,
};

struct CheckpointReviewSummary {
    std::string format_version{std::string(kCheckpointReviewSummaryFormatVersion)};
    std::string source_checkpoint_record_format_version{
        std::string(kCheckpointRecordFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    scheduler_snapshot::SchedulerSnapshotStatus snapshot_status{
        scheduler_snapshot::SchedulerSnapshotStatus::Waiting};
    CheckpointRecordStatus checkpoint_status{CheckpointRecordStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    CheckpointBasisKind basis_kind{CheckpointBasisKind::LocalOnly};
    bool checkpoint_friendly_source{true};
    std::size_t persistable_prefix_size{0};
    std::vector<std::string> persistable_prefix;
    std::optional<std::string> resume_candidate_node_name;
    bool resume_ready{false};
    std::optional<CheckpointResumeBlocker> resume_blocker;
    std::optional<std::string> terminal_reason;
    CheckpointReviewNextActionKind next_action{
        CheckpointReviewNextActionKind::AwaitCheckpointReadiness};
    std::string checkpoint_boundary_summary;
    std::string resume_preview;
    std::string next_step_recommendation;
};

struct CheckpointReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct CheckpointReviewSummaryResult {
    std::optional<CheckpointReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] CheckpointReviewSummaryValidationResult
validate_checkpoint_review_summary(const CheckpointReviewSummary &summary);

[[nodiscard]] CheckpointReviewSummaryResult
build_checkpoint_review_summary(const CheckpointRecord &record);

} // namespace ahfl::checkpoint_record

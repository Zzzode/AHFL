#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/persistence_descriptor/descriptor.hpp"

namespace ahfl::persistence_descriptor {

inline constexpr std::string_view kPersistenceReviewSummaryFormatVersion =
    "ahfl.persistence-review.v1";

enum class PersistenceReviewNextActionKind {
    ExportPersistenceHandoff,
    AwaitPersistenceReadiness,
    WorkflowCompleted,
    InvestigateFailure,
    PreservePartialState,
};

struct PersistenceReviewSummary {
    std::string format_version{std::string(kPersistenceReviewSummaryFormatVersion)};
    std::string source_persistence_descriptor_format_version{
        std::string(kPersistenceDescriptorFormatVersion)};
    std::string source_checkpoint_record_format_version{
        std::string(checkpoint_record::kCheckpointRecordFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    checkpoint_record::CheckpointRecordStatus checkpoint_status{
        checkpoint_record::CheckpointRecordStatus::Blocked};
    PersistenceDescriptorStatus persistence_status{PersistenceDescriptorStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string planned_durable_identity;
    PersistenceBasisKind export_basis_kind{PersistenceBasisKind::LocalPlanningOnly};
    std::size_t exportable_prefix_size{0};
    std::vector<std::string> exportable_prefix;
    std::optional<std::string> next_export_candidate_node_name;
    bool export_ready{false};
    std::optional<PersistenceBlocker> persistence_blocker;
    PersistenceReviewNextActionKind next_action{
        PersistenceReviewNextActionKind::AwaitPersistenceReadiness};
    std::string store_boundary_summary;
    std::string export_preview;
    std::string next_step_recommendation;
};

struct PersistenceReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PersistenceReviewSummaryResult {
    std::optional<PersistenceReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] PersistenceReviewSummaryValidationResult
validate_persistence_review_summary(const PersistenceReviewSummary &summary);

[[nodiscard]] PersistenceReviewSummaryResult
build_persistence_review_summary(const CheckpointPersistenceDescriptor &descriptor);

} // namespace ahfl::persistence_descriptor

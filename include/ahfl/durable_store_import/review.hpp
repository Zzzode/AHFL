#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/durable_store_import/request.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kReviewSummaryFormatVersion =
    "ahfl.durable-store-import-review.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view kDurableStoreImportReviewSummaryFormatVersion =
    kReviewSummaryFormatVersion;

enum class ReviewNextActionKind {
    HandoffDurableStoreImportRequest,
    AwaitAdapterReadiness,
    ArchiveCompletedDurableStoreImportState,
    InvestigateDurableStoreImportFailure,
    PreservePartialDurableStoreImportState,
};

// Legacy alias
using DurableStoreImportReviewNextActionKind [[deprecated("Use ReviewNextActionKind")]] =
    ReviewNextActionKind;

struct ReviewSummary {
    std::string format_version{std::string(kReviewSummaryFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kRequestFormatVersion)};
    std::string source_store_import_descriptor_format_version{
        std::string(store_import::kStoreImportDescriptorFormatVersion)};
    std::string source_export_manifest_format_version{
        std::string(persistence_export::kPersistenceExportManifestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    checkpoint_record::CheckpointRecordStatus checkpoint_status{
        checkpoint_record::CheckpointRecordStatus::Blocked};
    persistence_descriptor::PersistenceDescriptorStatus persistence_status{
        persistence_descriptor::PersistenceDescriptorStatus::Blocked};
    persistence_export::PersistenceExportManifestStatus manifest_status{
        persistence_export::PersistenceExportManifestStatus::Blocked};
    store_import::StoreImportDescriptorStatus descriptor_status{
        store_import::StoreImportDescriptorStatus::Blocked};
    RequestStatus request_status{RequestStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string planned_durable_identity;
    RequestBoundaryKind request_boundary_kind{RequestBoundaryKind::LocalIntentOnly};
    std::size_t requested_artifact_entry_count{0};
    std::vector<std::string> requested_artifact_preview;
    bool adapter_ready{false};
    std::optional<RequestedArtifactKind> next_required_adapter_artifact_kind;
    std::optional<RequestBlocker> adapter_blocker; // Keep field name for JSON compatibility
    ReviewNextActionKind next_action{ReviewNextActionKind::AwaitAdapterReadiness};
    std::string adapter_boundary_summary;
    std::string request_preview;
    std::string next_step_recommendation;
};

// Legacy alias for backward compatibility
using DurableStoreImportReviewSummary [[deprecated("Use ReviewSummary")]] = ReviewSummary;

struct ReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportReviewSummaryValidationResult
    [[deprecated("Use ReviewSummaryValidationResult")]] = ReviewSummaryValidationResult;

struct ReviewSummaryResult {
    std::optional<ReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportReviewSummaryResult [[deprecated("Use ReviewSummaryResult")]] =
    ReviewSummaryResult;

[[nodiscard]] ReviewSummaryValidationResult validate_review_summary(const ReviewSummary &summary);

[[nodiscard]] ReviewSummaryResult build_review_summary(const Request &request);

// Legacy function names - delegate to new functions
[[nodiscard]] inline ReviewSummaryValidationResult
validate_durable_store_import_review_summary(const ReviewSummary &summary) {
    return validate_review_summary(summary);
}

[[nodiscard]] inline ReviewSummaryResult
build_durable_store_import_review_summary(const Request &request) {
    return build_review_summary(request);
}

} // namespace ahfl::durable_store_import

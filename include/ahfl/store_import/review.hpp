#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/store_import/descriptor.hpp"

namespace ahfl::store_import {

inline constexpr std::string_view kStoreImportReviewSummaryFormatVersion =
    "ahfl.store-import-review.v1";

enum class StoreImportReviewNextActionKind {
    HandoffStoreImportDescriptor,
    AwaitStagingReadiness,
    ArchiveCompletedStoreImportState,
    InvestigateStoreImportFailure,
    PreservePartialStoreImportState,
};

struct StoreImportReviewSummary {
    std::string format_version{std::string(kStoreImportReviewSummaryFormatVersion)};
    std::string source_store_import_descriptor_format_version{
        std::string(kStoreImportDescriptorFormatVersion)};
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
    StoreImportDescriptorStatus descriptor_status{StoreImportDescriptorStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string planned_durable_identity;
    StoreImportBoundaryKind descriptor_boundary_kind{
        StoreImportBoundaryKind::LocalStagingOnly};
    std::size_t staging_artifact_entry_count{0};
    std::vector<std::string> staging_artifact_preview;
    bool import_ready{false};
    std::optional<StagingArtifactKind> next_required_staging_artifact_kind;
    std::optional<StagingBlocker> staging_blocker;
    StoreImportReviewNextActionKind next_action{
        StoreImportReviewNextActionKind::AwaitStagingReadiness};
    std::string store_boundary_summary;
    std::string staging_preview;
    std::string next_step_recommendation;
};

struct StoreImportReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct StoreImportReviewSummaryResult {
    std::optional<StoreImportReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] StoreImportReviewSummaryValidationResult
validate_store_import_review_summary(const StoreImportReviewSummary &summary);

[[nodiscard]] StoreImportReviewSummaryResult
build_store_import_review_summary(const StoreImportDescriptor &descriptor);

} // namespace ahfl::store_import

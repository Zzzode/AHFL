#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/persistence_export/manifest.hpp"

namespace ahfl::persistence_export {

inline constexpr std::string_view kPersistenceExportReviewSummaryFormatVersion =
    "ahfl.persistence-export-review.v1";

enum class PersistenceExportReviewNextActionKind {
    HandoffExportPackage,
    AwaitManifestReadiness,
    ArchiveCompletedExportPackage,
    InvestigateExportFailure,
    PreservePartialExportState,
};

struct PersistenceExportReviewSummary {
    std::string format_version{std::string(kPersistenceExportReviewSummaryFormatVersion)};
    std::string source_export_manifest_format_version{
        std::string(kPersistenceExportManifestFormatVersion)};
    std::string source_persistence_descriptor_format_version{
        std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion)};
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
    persistence_descriptor::PersistenceDescriptorStatus persistence_status{
        persistence_descriptor::PersistenceDescriptorStatus::Blocked};
    PersistenceExportManifestStatus manifest_status{
        PersistenceExportManifestStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string planned_durable_identity;
    ManifestBoundaryKind manifest_boundary_kind{ManifestBoundaryKind::LocalBundleOnly};
    std::size_t artifact_bundle_entry_count{0};
    std::vector<std::string> artifact_bundle_preview;
    bool manifest_ready{false};
    std::optional<ExportArtifactKind> next_required_artifact_kind;
    std::optional<StoreImportBlocker> store_import_blocker;
    PersistenceExportReviewNextActionKind next_action{
        PersistenceExportReviewNextActionKind::AwaitManifestReadiness};
    std::string store_boundary_summary;
    std::string import_preview;
    std::string next_step_recommendation;
};

struct PersistenceExportReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PersistenceExportReviewSummaryResult {
    std::optional<PersistenceExportReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] PersistenceExportReviewSummaryValidationResult
validate_persistence_export_review_summary(const PersistenceExportReviewSummary &summary);

[[nodiscard]] PersistenceExportReviewSummaryResult
build_persistence_export_review_summary(const PersistenceExportManifest &manifest);

} // namespace ahfl::persistence_export

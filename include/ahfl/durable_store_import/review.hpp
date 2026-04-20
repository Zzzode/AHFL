#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/durable_store_import/request.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kDurableStoreImportReviewSummaryFormatVersion =
    "ahfl.durable-store-import-review.v1";

enum class DurableStoreImportReviewNextActionKind {
    HandoffDurableStoreImportRequest,
    AwaitAdapterReadiness,
    ArchiveCompletedDurableStoreImportState,
    InvestigateDurableStoreImportFailure,
    PreservePartialDurableStoreImportState,
};

struct DurableStoreImportReviewSummary {
    std::string format_version{std::string(kDurableStoreImportReviewSummaryFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kDurableStoreImportRequestFormatVersion)};
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
    DurableStoreImportRequestStatus request_status{DurableStoreImportRequestStatus::Blocked};
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
    std::optional<AdapterBlocker> adapter_blocker;
    DurableStoreImportReviewNextActionKind next_action{
        DurableStoreImportReviewNextActionKind::AwaitAdapterReadiness};
    std::string adapter_boundary_summary;
    std::string request_preview;
    std::string next_step_recommendation;
};

struct DurableStoreImportReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DurableStoreImportReviewSummaryResult {
    std::optional<DurableStoreImportReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DurableStoreImportReviewSummaryValidationResult
validate_durable_store_import_review_summary(
    const DurableStoreImportReviewSummary &summary);

[[nodiscard]] DurableStoreImportReviewSummaryResult
build_durable_store_import_review_summary(const DurableStoreImportRequest &request);

} // namespace ahfl::durable_store_import

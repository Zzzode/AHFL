#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kDurableStoreImportRequestFormatVersion =
    "ahfl.durable-store-import-request.v1";

enum class DurableStoreImportRequestStatus {
    ReadyForAdapter,
    Blocked,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class RequestBoundaryKind {
    LocalIntentOnly,
    AdapterContractConsumable,
};

enum class RequestedArtifactKind {
    StoreImportDescriptor,
    ExportManifest,
    PersistenceDescriptor,
    PersistenceReview,
    CheckpointRecord,
};

enum class RequestedArtifactAdapterRole {
    RequestAnchor,
    ImportManifest,
    DurableStateDescriptor,
    HumanReviewContext,
    CheckpointPayload,
};

enum class AdapterBlockerKind {
    WaitingOnRequestedArtifact,
    MissingDurableStoreImportRequestIdentity,
    MissingRequestedArtifactSet,
    WorkflowFailure,
    WorkflowPartial,
};

struct RequestedArtifactEntry {
    RequestedArtifactKind artifact_kind{RequestedArtifactKind::StoreImportDescriptor};
    std::string logical_artifact_name;
    std::string source_format_version;
    bool required_for_import{true};
    RequestedArtifactAdapterRole adapter_role{RequestedArtifactAdapterRole::RequestAnchor};
};

struct RequestedArtifactSet {
    std::size_t entry_count{0};
    std::vector<RequestedArtifactEntry> entries;
};

struct AdapterBlocker {
    AdapterBlockerKind kind{AdapterBlockerKind::WaitingOnRequestedArtifact};
    std::string message;
    std::optional<std::string> logical_artifact_name;
};

struct DurableStoreImportRequest {
    std::string format_version{std::string(kDurableStoreImportRequestFormatVersion)};
    std::string source_store_import_descriptor_format_version{
        std::string(store_import::kStoreImportDescriptorFormatVersion)};
    std::string source_execution_plan_format_version{
        std::string(handoff::kExecutionPlanFormatVersion)};
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    std::string source_execution_journal_format_version{
        std::string(execution_journal::kExecutionJournalFormatVersion)};
    std::string source_replay_view_format_version{
        std::string(replay_view::kReplayViewFormatVersion)};
    std::string source_scheduler_snapshot_format_version{
        std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion)};
    std::string source_checkpoint_record_format_version{
        std::string(checkpoint_record::kCheckpointRecordFormatVersion)};
    std::string source_persistence_descriptor_format_version{
        std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion)};
    std::string source_export_manifest_format_version{
        std::string(persistence_export::kPersistenceExportManifestFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
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
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string planned_durable_identity;
    RequestBoundaryKind request_boundary_kind{RequestBoundaryKind::LocalIntentOnly};
    RequestedArtifactSet requested_artifact_set;
    bool adapter_ready{false};
    std::optional<RequestedArtifactKind> next_required_adapter_artifact_kind;
    DurableStoreImportRequestStatus request_status{DurableStoreImportRequestStatus::Blocked};
    std::optional<AdapterBlocker> adapter_blocker;
};

struct DurableStoreImportRequestValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DurableStoreImportRequestResult {
    std::optional<DurableStoreImportRequest> request;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DurableStoreImportRequestValidationResult
validate_durable_store_import_request(const DurableStoreImportRequest &request);

[[nodiscard]] DurableStoreImportRequestResult
build_durable_store_import_request(const store_import::StoreImportDescriptor &descriptor);

} // namespace ahfl::durable_store_import

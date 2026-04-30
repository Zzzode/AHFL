#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kPersistenceRequestFormatVersion =
    "ahfl.durable-store-import-decision-receipt-persistence-request.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion =
        kPersistenceRequestFormatVersion;

enum class PersistenceRequestStatus {
    ReadyToPersist,
    Blocked,
    Deferred,
    Rejected,
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceRequestStatus
    [[deprecated("Use PersistenceRequestStatus")]] = PersistenceRequestStatus;

enum class PersistenceRequestOutcome {
    PersistReadyReceipt,
    BlockBlockedReceipt,
    DeferPartialReceipt,
    RejectFailedReceipt,
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceRequestOutcome
    [[deprecated("Use PersistenceRequestOutcome")]] = PersistenceRequestOutcome;

enum class PersistenceBoundaryKind {
    LocalContractOnly,
    AdapterReceiptPersistenceConsumable,
};

// Legacy alias
using ReceiptPersistenceBoundaryKind
    [[deprecated("Use PersistenceBoundaryKind")]] = PersistenceBoundaryKind;

enum class PersistenceBlockerKind {
    SourceReceiptBlocked,
    MissingRequiredAdapterCapability,
    PartialWorkflowState,
    WorkflowFailure,
};

// Legacy alias
using ReceiptPersistenceBlockerKind
    [[deprecated("Use PersistenceBlockerKind")]] = PersistenceBlockerKind;

struct PersistenceBlocker {
    PersistenceBlockerKind kind{PersistenceBlockerKind::SourceReceiptBlocked};
    std::string message;
    std::optional<AdapterCapabilityKind> required_capability;
};

// Legacy alias
using ReceiptPersistenceBlocker
    [[deprecated("Use PersistenceBlocker")]] = PersistenceBlocker;

struct PersistenceRequest {
    std::string format_version{std::string(kPersistenceRequestFormatVersion)};
    std::string source_durable_store_import_decision_receipt_format_version{
        std::string(kReceiptFormatVersion)};
    std::string source_durable_store_import_decision_format_version{
        std::string(kDecisionFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kRequestFormatVersion)};
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
    RequestStatus request_status{RequestStatus::Blocked};
    DecisionStatus decision_status{DecisionStatus::Blocked};
    ReceiptStatus receipt_status{ReceiptStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string planned_durable_identity;
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    PersistenceBoundaryKind receipt_persistence_boundary_kind{  // Keep for JSON compatibility
        PersistenceBoundaryKind::LocalContractOnly};
    PersistenceRequestStatus receipt_persistence_request_status{  // Keep for JSON compatibility
        PersistenceRequestStatus::Blocked};
    PersistenceRequestOutcome receipt_persistence_request_outcome{  // Keep for JSON compatibility
        PersistenceRequestOutcome::BlockBlockedReceipt};
    bool accepted_for_receipt_persistence{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<PersistenceBlocker> receipt_persistence_blocker;  // Keep for JSON compatibility
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceiptPersistenceRequest
    [[deprecated("Use PersistenceRequest")]] = PersistenceRequest;

struct PersistenceRequestValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceRequestValidationResult
    [[deprecated("Use PersistenceRequestValidationResult")]] = PersistenceRequestValidationResult;

struct PersistenceRequestResult {
    std::optional<PersistenceRequest> request;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceRequestResult
    [[deprecated("Use PersistenceRequestResult")]] = PersistenceRequestResult;

[[nodiscard]] PersistenceRequestValidationResult
validate_persistence_request(const PersistenceRequest &request);

[[nodiscard]] PersistenceRequestResult
build_persistence_request(const Receipt &receipt);

// Legacy function names - delegate to new functions
[[nodiscard]] inline PersistenceRequestValidationResult
validate_durable_store_import_decision_receipt_persistence_request(
    const PersistenceRequest &request) {
    return validate_persistence_request(request);
}

[[nodiscard]] inline PersistenceRequestResult
build_durable_store_import_decision_receipt_persistence_request(const Receipt &receipt) {
    return build_persistence_request(receipt);
}

} // namespace ahfl::durable_store_import

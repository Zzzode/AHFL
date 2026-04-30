#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt_persistence.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kPersistenceResponseFormatVersion =
    "ahfl.durable-store-import-decision-receipt-persistence-response.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceResponseFormatVersion =
        kPersistenceResponseFormatVersion;

enum class PersistenceResponseStatus {
    Accepted,
    Blocked,
    Deferred,
    Rejected,
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseStatus
    [[deprecated("Use PersistenceResponseStatus")]] = PersistenceResponseStatus;

enum class PersistenceResponseOutcome {
    AcceptPersistenceRequest,
    BlockBlockedRequest,
    DeferDeferredRequest,
    RejectFailedRequest,
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseOutcome
    [[deprecated("Use PersistenceResponseOutcome")]] = PersistenceResponseOutcome;

enum class PersistenceResponseBoundaryKind {
    LocalContractOnly,
    AdapterResponseConsumable,
};

// Legacy alias
using ReceiptPersistenceResponseBoundaryKind
    [[deprecated("Use PersistenceResponseBoundaryKind")]] = PersistenceResponseBoundaryKind;

enum class PersistenceResponseBlockerKind {
    SourcePersistenceRequestBlocked,
    MissingRequiredAdapterCapability,
    PartialPersistenceState,
    PersistenceWorkflowFailure,
};

// Legacy alias
using ReceiptPersistenceResponseBlockerKind
    [[deprecated("Use PersistenceResponseBlockerKind")]] = PersistenceResponseBlockerKind;

struct PersistenceResponseBlocker {
    PersistenceResponseBlockerKind kind{
        PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked};
    std::string message;
    std::optional<AdapterCapabilityKind> required_capability;
};

// Legacy alias
using ReceiptPersistenceResponseBlocker
    [[deprecated("Use PersistenceResponseBlocker")]] = PersistenceResponseBlocker;

struct PersistenceResponse {
    std::string format_version{std::string(kPersistenceResponseFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_request_format_version{
        std::string(kPersistenceRequestFormatVersion)};
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
    PersistenceRequestStatus persistence_request_status{  // Keep field name for JSON
        PersistenceRequestStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string durable_store_import_receipt_persistence_response_identity;
    std::string planned_durable_identity;
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    PersistenceBoundaryKind receipt_persistence_boundary_kind{  // Keep field name
        PersistenceBoundaryKind::LocalContractOnly};
    PersistenceResponseBoundaryKind receipt_persistence_response_boundary_kind{  // Keep field name
        PersistenceResponseBoundaryKind::LocalContractOnly};
    PersistenceResponseStatus response_status{  // Keep field name
        PersistenceResponseStatus::Blocked};
    PersistenceResponseOutcome response_outcome{  // Keep field name
        PersistenceResponseOutcome::BlockBlockedRequest};
    bool acknowledged_for_response{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<PersistenceResponseBlocker> response_blocker;  // Keep field name
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceiptPersistenceResponse
    [[deprecated("Use PersistenceResponse")]] = PersistenceResponse;

struct PersistenceResponseValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseValidationResult
    [[deprecated("Use PersistenceResponseValidationResult")]] = PersistenceResponseValidationResult;

struct PersistenceResponseResult {
    std::optional<PersistenceResponse> response;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseResult
    [[deprecated("Use PersistenceResponseResult")]] = PersistenceResponseResult;

[[nodiscard]] PersistenceResponseValidationResult
validate_persistence_response(const PersistenceResponse &response);

[[nodiscard]] PersistenceResponseResult
build_persistence_response(const PersistenceRequest &request);

// Legacy function names - delegate to new functions
[[nodiscard]] inline PersistenceResponseValidationResult
validate_durable_store_import_decision_receipt_persistence_response(
    const PersistenceResponse &response) {
    return validate_persistence_response(response);
}

[[nodiscard]] inline PersistenceResponseResult
build_durable_store_import_decision_receipt_persistence_response(
    const PersistenceRequest &request) {
    return build_persistence_response(request);
}

} // namespace ahfl::durable_store_import

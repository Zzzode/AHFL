#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion =
        "ahfl.durable-store-import-decision-receipt-persistence-request.v1";

enum class DurableStoreImportDecisionReceiptPersistenceRequestStatus {
    ReadyToPersist,
    Blocked,
    Deferred,
    Rejected,
};

enum class DurableStoreImportDecisionReceiptPersistenceRequestOutcome {
    PersistReadyReceipt,
    BlockBlockedReceipt,
    DeferPartialReceipt,
    RejectFailedReceipt,
};

enum class ReceiptPersistenceBoundaryKind {
    LocalContractOnly,
    AdapterReceiptPersistenceConsumable,
};

enum class ReceiptPersistenceBlockerKind {
    SourceReceiptBlocked,
    MissingRequiredAdapterCapability,
    PartialWorkflowState,
    WorkflowFailure,
};

struct ReceiptPersistenceBlocker {
    ReceiptPersistenceBlockerKind kind{ReceiptPersistenceBlockerKind::SourceReceiptBlocked};
    std::string message;
    std::optional<AdapterCapabilityKind> required_capability;
};

struct DurableStoreImportDecisionReceiptPersistenceRequest {
    std::string format_version{
        std::string(kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion)};
    std::string source_durable_store_import_decision_receipt_format_version{
        std::string(kDurableStoreImportDecisionReceiptFormatVersion)};
    std::string source_durable_store_import_decision_format_version{
        std::string(kDurableStoreImportDecisionFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kDurableStoreImportRequestFormatVersion)};
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
    DurableStoreImportRequestStatus request_status{DurableStoreImportRequestStatus::Blocked};
    DurableStoreImportDecisionStatus decision_status{
        DurableStoreImportDecisionStatus::Blocked};
    DurableStoreImportDecisionReceiptStatus receipt_status{
        DurableStoreImportDecisionReceiptStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string planned_durable_identity;
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    ReceiptPersistenceBoundaryKind receipt_persistence_boundary_kind{
        ReceiptPersistenceBoundaryKind::LocalContractOnly};
    DurableStoreImportDecisionReceiptPersistenceRequestStatus receipt_persistence_request_status{
        DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked};
    DurableStoreImportDecisionReceiptPersistenceRequestOutcome receipt_persistence_request_outcome{
        DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt};
    bool accepted_for_receipt_persistence{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<ReceiptPersistenceBlocker> receipt_persistence_blocker;
};

struct DurableStoreImportDecisionReceiptPersistenceRequestValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DurableStoreImportDecisionReceiptPersistenceRequestResult {
    std::optional<DurableStoreImportDecisionReceiptPersistenceRequest> request;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DurableStoreImportDecisionReceiptPersistenceRequestValidationResult
validate_durable_store_import_decision_receipt_persistence_request(
    const DurableStoreImportDecisionReceiptPersistenceRequest &request);

[[nodiscard]] DurableStoreImportDecisionReceiptPersistenceRequestResult
build_durable_store_import_decision_receipt_persistence_request(
    const DurableStoreImportDecisionReceipt &receipt);

} // namespace ahfl::durable_store_import

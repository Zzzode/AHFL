#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/common.hpp"
#include "ahfl/durable_store_import/decision.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kReceiptFormatVersion =
    "ahfl.durable-store-import-decision-receipt.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view kDurableStoreImportDecisionReceiptFormatVersion =
    kReceiptFormatVersion;

enum class ReceiptStatus {
    ReadyForArchive,
    Blocked,
    Deferred,
    Rejected,
};

// Legacy alias
using DurableStoreImportDecisionReceiptStatus [[deprecated("Use ReceiptStatus")]] = ReceiptStatus;

enum class ReceiptOutcome {
    ArchiveAcceptedDecision,
    BlockBlockedDecision,
    DeferPartialDecision,
    RejectFailedDecision,
};

// Legacy alias
using DurableStoreImportDecisionReceiptOutcome [[deprecated("Use ReceiptOutcome")]] =
    ReceiptOutcome;

enum class ReceiptBoundaryKind {
    LocalContractOnly,
    AdapterReceiptConsumable,
};

enum class ReceiptBlockerKind {
    SourceDecisionBlocked,
    MissingRequiredAdapterCapability,
    PartialWorkflowState,
    WorkflowFailure,
};

struct ReceiptBlocker {
    ReceiptBlockerKind kind{ReceiptBlockerKind::SourceDecisionBlocked};
    std::string message;
    std::optional<AdapterCapabilityKind> required_capability;
};

struct Receipt {
    std::string format_version{std::string(kReceiptFormatVersion)};
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
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string planned_durable_identity;
    DecisionBoundaryKind decision_boundary_kind{DecisionBoundaryKind::LocalContractOnly};
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    ReceiptStatus receipt_status{ReceiptStatus::Blocked};
    ReceiptOutcome receipt_outcome{ReceiptOutcome::BlockBlockedDecision};
    bool accepted_for_receipt_archive{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<ReceiptBlocker> receipt_blocker;
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceipt [[deprecated("Use Receipt")]] = Receipt;

struct ReceiptValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptValidationResult
    [[deprecated("Use ReceiptValidationResult")]] = ReceiptValidationResult;

struct ReceiptResult {
    std::optional<Receipt> receipt;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptResult [[deprecated("Use ReceiptResult")]] = ReceiptResult;

[[nodiscard]] ReceiptValidationResult validate_receipt(const Receipt &receipt);
[[nodiscard]] ReceiptResult build_receipt(const Decision &decision);

// Legacy function names - delegate to new functions
[[nodiscard]] inline ReceiptValidationResult
validate_durable_store_import_decision_receipt(const Receipt &receipt) {
    return validate_receipt(receipt);
}

[[nodiscard]] inline ReceiptResult
build_durable_store_import_decision_receipt(const Decision &decision) {
    return build_receipt(decision);
}

} // namespace ahfl::durable_store_import

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt_persistence.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceReviewSummaryFormatVersion =
        "ahfl.durable-store-import-decision-receipt-persistence-review.v1";

enum class DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind {
    HandoffDurableStoreImportDecisionReceiptPersistenceRequest,
    ResolveRequiredAdapterCapability,
    PersistCompletedDurableStoreImportDecisionReceipt,
    PreservePartialDurableStoreImportDecisionReceipt,
    InvestigateDurableStoreImportDecisionReceiptPersistenceRejection,
};

struct DurableStoreImportDecisionReceiptPersistenceReviewSummary {
    std::string format_version{
        std::string(kDurableStoreImportDecisionReceiptPersistenceReviewSummaryFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_request_format_version{
        std::string(kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion)};
    std::string source_durable_store_import_decision_receipt_format_version{
        std::string(kDurableStoreImportDecisionReceiptFormatVersion)};
    std::string source_durable_store_import_decision_format_version{
        std::string(kDurableStoreImportDecisionFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kDurableStoreImportRequestFormatVersion)};
    std::string source_store_import_descriptor_format_version{
        std::string(store_import::kStoreImportDescriptorFormatVersion)};
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
    DurableStoreImportDecisionReceiptPersistenceRequestStatus receipt_persistence_request_status{
        DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked};
    DurableStoreImportDecisionReceiptPersistenceRequestOutcome receipt_persistence_request_outcome{
        DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt};
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
    bool accepted_for_receipt_persistence{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<ReceiptPersistenceBlocker> receipt_persistence_blocker;
    DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind next_action{
        DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind::
            ResolveRequiredAdapterCapability};
    std::string adapter_receipt_persistence_contract_summary;
    std::string persistence_preview;
    std::string next_step_recommendation;
};

struct DurableStoreImportDecisionReceiptPersistenceReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DurableStoreImportDecisionReceiptPersistenceReviewSummaryResult {
    std::optional<DurableStoreImportDecisionReceiptPersistenceReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DurableStoreImportDecisionReceiptPersistenceReviewSummaryValidationResult
validate_durable_store_import_decision_receipt_persistence_review_summary(
    const DurableStoreImportDecisionReceiptPersistenceReviewSummary &summary);

[[nodiscard]] DurableStoreImportDecisionReceiptPersistenceReviewSummaryResult
build_durable_store_import_decision_receipt_persistence_review_summary(
    const DurableStoreImportDecisionReceiptPersistenceRequest &request);

} // namespace ahfl::durable_store_import

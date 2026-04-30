#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt_persistence.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kPersistenceReviewFormatVersion =
    "ahfl.durable-store-import-decision-receipt-persistence-review.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceReviewSummaryFormatVersion =
        kPersistenceReviewFormatVersion;

enum class PersistenceReviewNextActionKind {
    HandoffDurableStoreImportDecisionReceiptPersistenceRequest,
    ResolveRequiredAdapterCapability,
    PersistCompletedDurableStoreImportDecisionReceipt,
    PreservePartialDurableStoreImportDecisionReceipt,
    InvestigateDurableStoreImportDecisionReceiptPersistenceRejection,
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind
    [[deprecated("Use PersistenceReviewNextActionKind")]] = PersistenceReviewNextActionKind;

struct PersistenceReviewSummary {
    std::string format_version{std::string(kPersistenceReviewFormatVersion)};
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
    PersistenceRequestStatus receipt_persistence_request_status{PersistenceRequestStatus::Blocked};
    PersistenceRequestOutcome receipt_persistence_request_outcome{
        PersistenceRequestOutcome::BlockBlockedReceipt};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string planned_durable_identity;
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    PersistenceBoundaryKind receipt_persistence_boundary_kind{
        PersistenceBoundaryKind::LocalContractOnly};
    bool accepted_for_receipt_persistence{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<PersistenceBlocker> receipt_persistence_blocker;
    PersistenceReviewNextActionKind next_action{
        PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability};
    std::string adapter_receipt_persistence_contract_summary;
    std::string persistence_preview;
    std::string next_step_recommendation;
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceiptPersistenceReviewSummary
    [[deprecated("Use PersistenceReviewSummary")]] = PersistenceReviewSummary;

struct PersistenceReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceReviewSummaryValidationResult
    [[deprecated("Use PersistenceReviewSummaryValidationResult")]] =
        PersistenceReviewSummaryValidationResult;

struct PersistenceReviewSummaryResult {
    std::optional<PersistenceReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceReviewSummaryResult
    [[deprecated("Use PersistenceReviewSummaryResult")]] = PersistenceReviewSummaryResult;

[[nodiscard]] PersistenceReviewSummaryValidationResult
validate_persistence_review_summary(const PersistenceReviewSummary &summary);

[[nodiscard]] PersistenceReviewSummaryResult
build_persistence_review_summary(const PersistenceRequest &request);

// Legacy function names - delegate to new functions
[[nodiscard]] inline PersistenceReviewSummaryValidationResult
validate_durable_store_import_decision_receipt_persistence_review_summary(
    const PersistenceReviewSummary &summary) {
    return validate_persistence_review_summary(summary);
}

[[nodiscard]] inline PersistenceReviewSummaryResult
build_durable_store_import_decision_receipt_persistence_review_summary(
    const PersistenceRequest &request) {
    return build_persistence_review_summary(request);
}

} // namespace ahfl::durable_store_import

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kReceiptReviewFormatVersion =
    "ahfl.durable-store-import-decision-receipt-review.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view kDurableStoreImportDecisionReceiptReviewSummaryFormatVersion =
    kReceiptReviewFormatVersion;

enum class ReceiptReviewNextActionKind {
    HandoffDurableStoreImportDecisionReceipt,
    ResolveRequiredAdapterCapability,
    ArchiveCompletedDurableStoreImportDecisionReceipt,
    PreservePartialDurableStoreImportDecisionReceipt,
    InvestigateDurableStoreImportDecisionReceiptRejection,
};

// Legacy alias
using DurableStoreImportDecisionReceiptReviewNextActionKind
    [[deprecated("Use ReceiptReviewNextActionKind")]] = ReceiptReviewNextActionKind;

struct ReceiptReviewSummary {
    std::string format_version{std::string(kReceiptReviewFormatVersion)};
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
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string planned_durable_identity;
    ReceiptBoundaryKind receipt_boundary_kind{ReceiptBoundaryKind::LocalContractOnly};
    ReceiptOutcome receipt_outcome{ReceiptOutcome::BlockBlockedDecision};
    bool accepted_for_receipt_archive{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<ReceiptBlocker> receipt_blocker;
    ReceiptReviewNextActionKind next_action{
        ReceiptReviewNextActionKind::ResolveRequiredAdapterCapability};
    std::string adapter_receipt_contract_summary;
    std::string receipt_preview;
    std::string next_step_recommendation;
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceiptReviewSummary [[deprecated("Use ReceiptReviewSummary")]] =
    ReceiptReviewSummary;

struct ReceiptReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptReviewSummaryValidationResult
    [[deprecated("Use ReceiptReviewSummaryValidationResult")]] =
        ReceiptReviewSummaryValidationResult;

struct ReceiptReviewSummaryResult {
    std::optional<ReceiptReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptReviewSummaryResult
    [[deprecated("Use ReceiptReviewSummaryResult")]] = ReceiptReviewSummaryResult;

[[nodiscard]] ReceiptReviewSummaryValidationResult
validate_receipt_review_summary(const ReceiptReviewSummary &summary);

[[nodiscard]] ReceiptReviewSummaryResult build_receipt_review_summary(const Receipt &receipt);

// Legacy function names - delegate to new functions
[[nodiscard]] inline ReceiptReviewSummaryValidationResult
validate_durable_store_import_decision_receipt_review_summary(const ReceiptReviewSummary &summary) {
    return validate_receipt_review_summary(summary);
}

[[nodiscard]] inline ReceiptReviewSummaryResult
build_durable_store_import_decision_receipt_review_summary(const Receipt &receipt) {
    return build_receipt_review_summary(receipt);
}

} // namespace ahfl::durable_store_import

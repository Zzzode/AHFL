#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt_persistence_response.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kPersistenceResponseReviewFormatVersion =
    "ahfl.durable-store-import-decision-receipt-persistence-response-review.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view
    kDurableStoreImportDecisionReceiptPersistenceResponseReviewFormatVersion =
        kPersistenceResponseReviewFormatVersion;

enum class PersistenceResponseReviewNextActionKind {
    AcknowledgeResponse,
    ResolveBlocker,
    WaitforCapability,
    ReviewFailure,
};

// Legacy alias
using ReceiptPersistenceResponseReviewNextActionKind
    [[deprecated("Use PersistenceResponseReviewNextActionKind")]] = PersistenceResponseReviewNextActionKind;

struct PersistenceResponsePreview {
    std::string response_identity;
    PersistenceResponseStatus response_status;
    PersistenceResponseOutcome response_outcome;
    bool acknowledged_for_response;
    PersistenceResponseBoundaryKind response_boundary_kind;
};

// Legacy alias
using ReceiptPersistenceResponsePreview
    [[deprecated("Use PersistenceResponsePreview")]] = PersistenceResponsePreview;

struct PersistenceResponseReviewSummary {
    std::string format_version{std::string(kPersistenceResponseReviewFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_response_format_version{
        std::string(kPersistenceResponseFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_request_format_version{
        std::string(kPersistenceRequestFormatVersion)};
    std::string source_durable_store_import_decision_receipt_format_version{
        std::string(kReceiptFormatVersion)};
    std::string source_durable_store_import_decision_format_version{
        std::string(kDecisionFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kRequestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string durable_store_import_receipt_persistence_response_identity;
    PersistenceResponseStatus response_status{PersistenceResponseStatus::Blocked};
    PersistenceResponseOutcome response_outcome{
        PersistenceResponseOutcome::BlockBlockedRequest};
    PersistenceResponseBoundaryKind response_boundary_kind{
        PersistenceResponseBoundaryKind::LocalContractOnly};
    bool acknowledged_for_response{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<PersistenceResponseBlocker> response_blocker;
    PersistenceResponsePreview response_preview;
    std::string adapter_response_contract_summary;
    std::string next_step_recommendation;
    PersistenceResponseReviewNextActionKind next_action{
        PersistenceResponseReviewNextActionKind::ResolveBlocker};
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReceiptPersistenceResponseReviewSummary
    [[deprecated("Use PersistenceResponseReviewSummary")]] = PersistenceResponseReviewSummary;

struct PersistenceResponseReviewValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseReviewValidationResult
    [[deprecated("Use PersistenceResponseReviewValidationResult")]] = PersistenceResponseReviewValidationResult;

struct PersistenceResponseReviewResult {
    std::optional<PersistenceResponseReviewSummary> review;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReceiptPersistenceResponseReviewResult
    [[deprecated("Use PersistenceResponseReviewResult")]] = PersistenceResponseReviewResult;

[[nodiscard]] PersistenceResponseReviewValidationResult
validate_persistence_response_review(const PersistenceResponseReviewSummary &review);

[[nodiscard]] PersistenceResponseReviewResult
build_persistence_response_review(const PersistenceResponse &response);

// Legacy function names - delegate to new functions
[[nodiscard]] inline PersistenceResponseReviewValidationResult
validate_durable_store_import_decision_receipt_persistence_response_review(
    const PersistenceResponseReviewSummary &review) {
    return validate_persistence_response_review(review);
}

[[nodiscard]] inline PersistenceResponseReviewResult
build_durable_store_import_decision_receipt_persistence_response_review(
    const PersistenceResponse &response) {
    return build_persistence_response_review(response);
}

} // namespace ahfl::durable_store_import

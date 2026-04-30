#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/decision.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kDecisionReviewFormatVersion =
    "ahfl.durable-store-import-decision-review.v1";

// Legacy alias for backward compatibility
inline constexpr std::string_view kDurableStoreImportDecisionReviewSummaryFormatVersion =
    kDecisionReviewFormatVersion;

enum class DecisionReviewNextActionKind {
    HandoffDurableStoreImportDecision,
    ResolveRequiredAdapterCapability,
    ArchiveCompletedDurableStoreImportDecision,
    PreservePartialDurableStoreImportDecision,
    InvestigateDurableStoreImportDecisionRejection,
};

// Legacy alias
using DurableStoreImportDecisionReviewNextActionKind
    [[deprecated("Use DecisionReviewNextActionKind")]] = DecisionReviewNextActionKind;

struct DecisionReviewSummary {
    std::string format_version{std::string(kDecisionReviewFormatVersion)};
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
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string planned_durable_identity;
    DecisionBoundaryKind decision_boundary_kind{DecisionBoundaryKind::LocalContractOnly};
    DecisionOutcome decision_outcome{DecisionOutcome::BlockRequest};
    bool accepted_for_future_execution{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<DecisionBlocker> decision_blocker;
    DecisionReviewNextActionKind next_action{
        DecisionReviewNextActionKind::ResolveRequiredAdapterCapability};
    std::string adapter_contract_summary;
    std::string decision_preview;
    std::string next_step_recommendation;
};

// Legacy alias for backward compatibility
using DurableStoreImportDecisionReviewSummary
    [[deprecated("Use DecisionReviewSummary")]] = DecisionReviewSummary;

struct DecisionReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReviewSummaryValidationResult
    [[deprecated("Use DecisionReviewSummaryValidationResult")]] = DecisionReviewSummaryValidationResult;

struct DecisionReviewSummaryResult {
    std::optional<DecisionReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Legacy alias
using DurableStoreImportDecisionReviewSummaryResult
    [[deprecated("Use DecisionReviewSummaryResult")]] = DecisionReviewSummaryResult;

[[nodiscard]] DecisionReviewSummaryValidationResult
validate_decision_review_summary(const DecisionReviewSummary &summary);

[[nodiscard]] DecisionReviewSummaryResult
build_decision_review_summary(const Decision &decision);

// Legacy function names - delegate to new functions
[[nodiscard]] inline DecisionReviewSummaryValidationResult
validate_durable_store_import_decision_review_summary(const DecisionReviewSummary &summary) {
    return validate_decision_review_summary(summary);
}

[[nodiscard]] inline DecisionReviewSummaryResult
build_durable_store_import_decision_review_summary(const Decision &decision) {
    return build_decision_review_summary(decision);
}

} // namespace ahfl::durable_store_import

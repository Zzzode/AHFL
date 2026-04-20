#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/decision.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kDurableStoreImportDecisionReviewSummaryFormatVersion =
    "ahfl.durable-store-import-decision-review.v1";

enum class DurableStoreImportDecisionReviewNextActionKind {
    HandoffDurableStoreImportDecision,
    ResolveRequiredAdapterCapability,
    ArchiveCompletedDurableStoreImportDecision,
    PreservePartialDurableStoreImportDecision,
    InvestigateDurableStoreImportDecisionRejection,
};

struct DurableStoreImportDecisionReviewSummary {
    std::string format_version{
        std::string(kDurableStoreImportDecisionReviewSummaryFormatVersion)};
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
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string durable_store_import_request_identity;
    std::string durable_store_import_decision_identity;
    std::string planned_durable_identity;
    DecisionBoundaryKind decision_boundary_kind{DecisionBoundaryKind::LocalContractOnly};
    DurableStoreImportDecisionOutcome decision_outcome{
        DurableStoreImportDecisionOutcome::BlockRequest};
    bool accepted_for_future_execution{false};
    std::optional<AdapterCapabilityKind> next_required_adapter_capability;
    std::optional<DecisionBlocker> decision_blocker;
    DurableStoreImportDecisionReviewNextActionKind next_action{
        DurableStoreImportDecisionReviewNextActionKind::ResolveRequiredAdapterCapability};
    std::string adapter_contract_summary;
    std::string decision_preview;
    std::string next_step_recommendation;
};

struct DurableStoreImportDecisionReviewSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DurableStoreImportDecisionReviewSummaryResult {
    std::optional<DurableStoreImportDecisionReviewSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DurableStoreImportDecisionReviewSummaryValidationResult
validate_durable_store_import_decision_review_summary(
    const DurableStoreImportDecisionReviewSummary &summary);

[[nodiscard]] DurableStoreImportDecisionReviewSummaryResult
build_durable_store_import_decision_review_summary(
    const DurableStoreImportDecision &decision);

} // namespace ahfl::durable_store_import

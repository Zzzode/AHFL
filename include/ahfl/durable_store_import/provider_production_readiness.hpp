#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderProductionReadinessEvidenceFormatVersion =
    "ahfl.durable-store-import-provider-production-readiness-evidence.v1";

inline constexpr std::string_view kProviderProductionReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-production-readiness-review.v1";

inline constexpr std::string_view kProviderProductionReadinessReportFormatVersion =
    "ahfl.durable-store-import-provider-production-readiness-report.v1";

enum class ProviderProductionReleaseGate {
    ReadyForProductionReview,
    Conditional,
    Blocked,
};

struct ProviderProductionReadinessEvidence {
    std::string format_version{std::string(kProviderProductionReadinessEvidenceFormatVersion)};
    std::string source_durable_store_import_provider_capability_negotiation_review_format_version{
        std::string(kProviderCapabilityNegotiationReviewFormatVersion)};
    std::string source_durable_store_import_provider_compatibility_report_format_version{
        std::string(kProviderCompatibilityReportFormatVersion)};
    std::string source_durable_store_import_provider_execution_audit_event_format_version{
        std::string(kProviderExecutionAuditEventFormatVersion)};
    std::string source_durable_store_import_provider_write_recovery_plan_format_version{
        std::string(kProviderWriteRecoveryPlanFormatVersion)};
    std::string source_durable_store_import_provider_failure_taxonomy_report_format_version{
        std::string(kProviderFailureTaxonomyReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_selection_plan_identity;
    std::string durable_store_import_provider_compatibility_report_identity;
    std::string durable_store_import_provider_execution_audit_event_identity;
    std::string durable_store_import_provider_write_recovery_plan_identity;
    std::string durable_store_import_provider_failure_taxonomy_report_identity;
    std::string durable_store_import_provider_production_readiness_evidence_identity;
    bool security_evidence_passed{false};
    bool recovery_evidence_passed{false};
    bool compatibility_evidence_passed{false};
    bool observability_evidence_passed{false};
    bool registry_evidence_passed{false};
    int blocking_issue_count{1};
    std::string evidence_summary;
};

struct ProviderProductionReadinessReview {
    std::string format_version{std::string(kProviderProductionReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_production_readiness_evidence_format_version{
        std::string(kProviderProductionReadinessEvidenceFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_production_readiness_evidence_identity;
    ProviderProductionReleaseGate release_gate{ProviderProductionReleaseGate::Blocked};
    int blocking_issue_count{1};
    std::string blocking_issue_summary;
    std::string readiness_summary;
};

struct ProviderProductionReadinessReport {
    std::string format_version{std::string(kProviderProductionReadinessReportFormatVersion)};
    std::string source_durable_store_import_provider_production_readiness_review_format_version{
        std::string(kProviderProductionReadinessReviewFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_production_readiness_evidence_identity;
    ProviderProductionReleaseGate release_gate{ProviderProductionReleaseGate::Blocked};
    std::string operator_report_summary;
    std::string next_step_recommendation;
};

struct ProviderProductionReadinessEvidenceValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderProductionReadinessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderProductionReadinessReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderProductionReadinessEvidenceResult {
    std::optional<ProviderProductionReadinessEvidence> evidence;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderProductionReadinessReviewResult {
    std::optional<ProviderProductionReadinessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderProductionReadinessReportResult {
    std::optional<ProviderProductionReadinessReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderProductionReadinessEvidenceValidationResult
validate_provider_production_readiness_evidence(
    const ProviderProductionReadinessEvidence &evidence);

[[nodiscard]] ProviderProductionReadinessReviewValidationResult
validate_provider_production_readiness_review(const ProviderProductionReadinessReview &review);

[[nodiscard]] ProviderProductionReadinessReportValidationResult
validate_provider_production_readiness_report(const ProviderProductionReadinessReport &report);

[[nodiscard]] ProviderProductionReadinessEvidenceResult
build_provider_production_readiness_evidence(
    const ProviderCapabilityNegotiationReview &negotiation_review,
    const ProviderCompatibilityReport &compatibility_report,
    const ProviderExecutionAuditEvent &audit_event,
    const ProviderWriteRecoveryPlan &recovery_plan,
    const ProviderFailureTaxonomyReport &taxonomy_report);

[[nodiscard]] ProviderProductionReadinessReviewResult
build_provider_production_readiness_review(const ProviderProductionReadinessEvidence &evidence);

[[nodiscard]] ProviderProductionReadinessReportResult
build_provider_production_readiness_report(const ProviderProductionReadinessReview &review);

} // namespace ahfl::durable_store_import

#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_PRODUCTION_READINESS";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string gate_slug(ProviderProductionReleaseGate gate) {
    switch (gate) {
    case ProviderProductionReleaseGate::ReadyForProductionReview:
        return "ready-for-production-review";
    case ProviderProductionReleaseGate::Conditional:
        return "conditional";
    case ProviderProductionReleaseGate::Blocked:
        return "blocked";
    }
    return "blocked";
}

} // namespace

ProviderProductionReadinessEvidenceValidationResult validate_provider_production_readiness_evidence(
    const ProviderProductionReadinessEvidence &evidence) {
    ProviderProductionReadinessEvidenceValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (evidence.format_version != kProviderProductionReadinessEvidenceFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider production readiness evidence format_version must be '" +
                                  std::string(kProviderProductionReadinessEvidenceFormatVersion) +
                                  "'");
    }
    if (evidence.source_durable_store_import_provider_capability_negotiation_review_format_version !=
            kProviderCapabilityNegotiationReviewFormatVersion ||
        evidence.source_durable_store_import_provider_compatibility_report_format_version !=
            kProviderCompatibilityReportFormatVersion ||
        evidence.source_durable_store_import_provider_execution_audit_event_format_version !=
            kProviderExecutionAuditEventFormatVersion ||
        evidence.source_durable_store_import_provider_write_recovery_plan_format_version !=
            kProviderWriteRecoveryPlanFormatVersion ||
        evidence.source_durable_store_import_provider_failure_taxonomy_report_format_version !=
            kProviderFailureTaxonomyReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider production readiness evidence source format_versions must "
                              "match V0.37-V0.41 artifacts");
    }
    if (evidence.workflow_canonical_name.empty() || evidence.session_id.empty() ||
        evidence.input_fixture.empty() ||
        evidence.durable_store_import_provider_selection_plan_identity.empty() ||
        evidence.durable_store_import_provider_compatibility_report_identity.empty() ||
        evidence.durable_store_import_provider_execution_audit_event_identity.empty() ||
        evidence.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        evidence.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        evidence.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        evidence.evidence_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider production readiness evidence identity and summary fields must not be empty");
    }
    if (evidence.blocking_issue_count < 0) {
        emit_validation_error(
            diagnostics, "provider production readiness blocking issue count cannot be negative");
    }
    return result;
}

ProviderProductionReadinessReviewValidationResult
validate_provider_production_readiness_review(const ProviderProductionReadinessReview &review) {
    ProviderProductionReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderProductionReadinessReviewFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider production readiness review format_version must be '" +
                                  std::string(kProviderProductionReadinessReviewFormatVersion) +
                                  "'");
    }
    if (review.source_durable_store_import_provider_production_readiness_evidence_format_version !=
        kProviderProductionReadinessEvidenceFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider production readiness review source format_version must be '" +
                std::string(kProviderProductionReadinessEvidenceFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        review.blocking_issue_summary.empty() || review.readiness_summary.empty()) {
        emit_validation_error(diagnostics,
                              "provider production readiness review fields must not be empty");
    }
    if (review.blocking_issue_count < 0) {
        emit_validation_error(
            diagnostics,
            "provider production readiness review blocking issue count cannot be negative");
    }
    return result;
}

ProviderProductionReadinessReportValidationResult
validate_provider_production_readiness_report(const ProviderProductionReadinessReport &report) {
    ProviderProductionReadinessReportValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (report.format_version != kProviderProductionReadinessReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider production readiness report format_version must be '" +
                                  std::string(kProviderProductionReadinessReportFormatVersion) +
                                  "'");
    }
    if (report.source_durable_store_import_provider_production_readiness_review_format_version !=
        kProviderProductionReadinessReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider production readiness report source format_version must be '" +
                std::string(kProviderProductionReadinessReviewFormatVersion) + "'");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        report.operator_report_summary.empty() || report.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "provider production readiness report fields must not be empty");
    }
    return result;
}

ProviderProductionReadinessEvidenceResult build_provider_production_readiness_evidence(
    const ProviderCapabilityNegotiationReview &negotiation_review,
    const ProviderCompatibilityReport &compatibility_report,
    const ProviderExecutionAuditEvent &audit_event,
    const ProviderWriteRecoveryPlan &recovery_plan,
    const ProviderFailureTaxonomyReport &taxonomy_report) {
    ProviderProductionReadinessEvidenceResult result;
    result.diagnostics.append(
        validate_provider_capability_negotiation_review(negotiation_review).diagnostics);
    result.diagnostics.append(
        validate_provider_compatibility_report(compatibility_report).diagnostics);
    result.diagnostics.append(validate_provider_execution_audit_event(audit_event).diagnostics);
    result.diagnostics.append(validate_provider_write_recovery_plan(recovery_plan).diagnostics);
    result.diagnostics.append(
        validate_provider_failure_taxonomy_report(taxonomy_report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderProductionReadinessEvidence evidence;
    evidence.workflow_canonical_name = compatibility_report.workflow_canonical_name;
    evidence.session_id = compatibility_report.session_id;
    evidence.run_id = compatibility_report.run_id;
    evidence.input_fixture = compatibility_report.input_fixture;
    evidence.durable_store_import_provider_selection_plan_identity =
        negotiation_review.durable_store_import_provider_selection_plan_identity;
    evidence.durable_store_import_provider_compatibility_report_identity =
        compatibility_report.durable_store_import_provider_compatibility_report_identity;
    evidence.durable_store_import_provider_execution_audit_event_identity =
        audit_event.durable_store_import_provider_execution_audit_event_identity;
    evidence.durable_store_import_provider_write_recovery_plan_identity =
        recovery_plan.durable_store_import_provider_write_recovery_plan_identity;
    evidence.durable_store_import_provider_failure_taxonomy_report_identity =
        taxonomy_report.durable_store_import_provider_failure_taxonomy_report_identity;
    evidence.security_evidence_passed = !taxonomy_report.secret_bearing_error_persisted &&
                                        !taxonomy_report.raw_provider_error_persisted;
    evidence.recovery_evidence_passed = recovery_plan.secret_free;
    evidence.compatibility_evidence_passed =
        compatibility_report.status == ProviderCompatibilityStatus::Passed;
    evidence.observability_evidence_passed =
        audit_event.secret_free && !audit_event.raw_telemetry_persisted;
    evidence.registry_evidence_passed =
        negotiation_review.negotiation_status == ProviderCapabilityNegotiationStatus::Compatible;
    evidence.blocking_issue_count =
        evidence.security_evidence_passed && evidence.recovery_evidence_passed &&
                evidence.compatibility_evidence_passed && evidence.observability_evidence_passed &&
                evidence.registry_evidence_passed
            ? 0
            : 1;
    evidence.durable_store_import_provider_production_readiness_evidence_identity =
        "durable-store-import-provider-production-readiness-evidence::" + evidence.session_id;
    evidence.evidence_summary = evidence.blocking_issue_count == 0
                                    ? "provider production readiness evidence is complete"
                                    : "provider production readiness evidence has blocking issues";

    result.diagnostics.append(
        validate_provider_production_readiness_evidence(evidence).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.evidence = std::move(evidence);
    return result;
}

ProviderProductionReadinessReviewResult
build_provider_production_readiness_review(const ProviderProductionReadinessEvidence &evidence) {
    ProviderProductionReadinessReviewResult result;
    result.diagnostics.append(
        validate_provider_production_readiness_evidence(evidence).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderProductionReadinessReview review;
    review.workflow_canonical_name = evidence.workflow_canonical_name;
    review.session_id = evidence.session_id;
    review.run_id = evidence.run_id;
    review.input_fixture = evidence.input_fixture;
    review.durable_store_import_provider_production_readiness_evidence_identity =
        evidence.durable_store_import_provider_production_readiness_evidence_identity;
    review.blocking_issue_count = evidence.blocking_issue_count;
    if (evidence.blocking_issue_count == 0) {
        review.release_gate = ProviderProductionReleaseGate::ReadyForProductionReview;
        review.blocking_issue_summary = "no blocking production readiness issues";
    } else if (evidence.compatibility_evidence_passed && evidence.observability_evidence_passed) {
        review.release_gate = ProviderProductionReleaseGate::Conditional;
        review.blocking_issue_summary = "conditional readiness requires operator follow-up";
    } else {
        review.release_gate = ProviderProductionReleaseGate::Blocked;
        review.blocking_issue_summary = "production readiness is blocked by missing evidence";
    }
    review.readiness_summary =
        "provider production release gate is " + gate_slug(review.release_gate);

    result.diagnostics.append(validate_provider_production_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

ProviderProductionReadinessReportResult
build_provider_production_readiness_report(const ProviderProductionReadinessReview &review) {
    ProviderProductionReadinessReportResult result;
    result.diagnostics.append(validate_provider_production_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderProductionReadinessReport report;
    report.workflow_canonical_name = review.workflow_canonical_name;
    report.session_id = review.session_id;
    report.run_id = review.run_id;
    report.input_fixture = review.input_fixture;
    report.durable_store_import_provider_production_readiness_evidence_identity =
        review.durable_store_import_provider_production_readiness_evidence_identity;
    report.release_gate = review.release_gate;
    report.operator_report_summary =
        "provider production readiness report gate is " + gate_slug(report.release_gate);
    report.next_step_recommendation =
        report.release_gate == ProviderProductionReleaseGate::ReadyForProductionReview
            ? "submit evidence package to organization production review"
            : "resolve blocking issues before production review";

    result.diagnostics.append(validate_provider_production_readiness_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import

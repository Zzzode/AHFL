// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_production_readiness.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/production_readiness.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_production_readiness_detail_kValidationDiagnosticCode{
        "DSI_PROVIDER_PRODUCTION_READINESS"};

void provider_production_readiness_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_production_readiness_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_production_readiness_detail_gate_slug(ProviderProductionReleaseGate gate) {
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
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness evidence format_version must be '" +
                std::string(kProviderProductionReadinessEvidenceFormatVersion) + "'");
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
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
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
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness evidence identity and summary fields must not be empty");
    }
    if (evidence.blocking_issue_count < 0) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics, "provider production readiness blocking issue count cannot be negative");
    }
    return result;
}

ProviderProductionReadinessReviewValidationResult
validate_provider_production_readiness_review(const ProviderProductionReadinessReview &review) {
    ProviderProductionReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderProductionReadinessReviewFormatVersion) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness review format_version must be '" +
                std::string(kProviderProductionReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_production_readiness_evidence_format_version !=
        kProviderProductionReadinessEvidenceFormatVersion) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness review source format_version must be '" +
                std::string(kProviderProductionReadinessEvidenceFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        review.blocking_issue_summary.empty() || review.readiness_summary.empty()) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics, "provider production readiness review fields must not be empty");
    }
    if (review.blocking_issue_count < 0) {
        provider_production_readiness_detail_emit_validation_error(
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
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness report format_version must be '" +
                std::string(kProviderProductionReadinessReportFormatVersion) + "'");
    }
    if (report.source_durable_store_import_provider_production_readiness_review_format_version !=
        kProviderProductionReadinessReviewFormatVersion) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics,
            "provider production readiness report source format_version must be '" +
                std::string(kProviderProductionReadinessReviewFormatVersion) + "'");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        report.operator_report_summary.empty() || report.next_step_recommendation.empty()) {
        provider_production_readiness_detail_emit_validation_error(
            diagnostics, "provider production readiness report fields must not be empty");
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
    review.readiness_summary = "provider production release gate is " +
                               provider_production_readiness_detail_gate_slug(review.release_gate);

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
        "provider production readiness report gate is " +
        provider_production_readiness_detail_gate_slug(report.release_gate);
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

// ---- provider_release_evidence_archive.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/release_evidence_archive.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_release_evidence_archive_detail_kValidationDiagnosticCode{
        "DSI_PROVIDER_RELEASE_EVIDENCE_ARCHIVE"};

void provider_release_evidence_archive_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                    std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_release_evidence_archive_detail_kValidationDiagnosticCode, message);
}

// Build a single evidence item (from the conformance report)
[[nodiscard]] EvidenceItem provider_release_evidence_archive_detail_make_conformance_evidence_item(
    const ProviderConformanceReport &report) {
    EvidenceItem item;
    item.evidence_type = "conformance";
    item.evidence_identity = report.durable_store_import_provider_conformance_report_identity;
    item.format_version = report.format_version;
    item.digest = "sha256:conformance:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.durable_store_import_provider_conformance_report_identity.empty();
    item.is_valid = (report.fail_count == 0);
    return item;
}

// Build a single evidence item (from the schema compatibility report)
[[nodiscard]] EvidenceItem
provider_release_evidence_archive_detail_make_schema_compatibility_evidence_item(
    const ProviderSchemaCompatibilityReport &report) {
    EvidenceItem item;
    item.evidence_type = "schema_compatibility";
    item.evidence_identity = "schema-compatibility-report::" + report.session_id;
    item.format_version = report.format_version;
    item.digest = "sha256:schema:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.format_version.empty();
    item.is_valid = !report.has_schema_drift;
    return item;
}

// Build a single evidence item (from the config bundle validation report)
[[nodiscard]] EvidenceItem
provider_release_evidence_archive_detail_make_config_validation_evidence_item(
    const ProviderConfigBundleValidationReport &report) {
    EvidenceItem item;
    item.evidence_type = "config_validation";
    item.evidence_identity = report.config_bundle_identity;
    item.format_version = report.format_version;
    item.digest = "sha256:config:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.config_bundle_identity.empty();
    item.is_valid = (report.invalid_count == 0 && !report.reads_secret_value &&
                     !report.opens_network_connection);
    return item;
}

// Build a single evidence item (from the production readiness evidence)
[[nodiscard]] EvidenceItem provider_release_evidence_archive_detail_make_readiness_evidence_item(
    const ProviderProductionReadinessEvidence &evidence) {
    EvidenceItem item;
    item.evidence_type = "readiness";
    item.evidence_identity =
        evidence.durable_store_import_provider_production_readiness_evidence_identity;
    item.format_version = evidence.format_version;
    item.digest = "sha256:readiness:" + evidence.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present =
        !evidence.durable_store_import_provider_production_readiness_evidence_identity.empty();
    item.is_valid = (evidence.blocking_issue_count == 0 && evidence.security_evidence_passed &&
                     evidence.recovery_evidence_passed && evidence.compatibility_evidence_passed);
    return item;
}

} // namespace

ReleaseEvidenceArchiveManifestValidationResult
validate_release_evidence_archive_manifest(const ReleaseEvidenceArchiveManifest &manifest) {
    ReleaseEvidenceArchiveManifestValidationResult result;
    auto &diagnostics = result.diagnostics;

    // Check format_version
    if (manifest.format_version != kProviderReleaseEvidenceArchiveManifestFormatVersion) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest format_version must be '" +
                std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion) + "'");
    }

    // Check that required fields are non-empty
    if (manifest.workflow_canonical_name.empty() || manifest.session_id.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest workflow_canonical_name and "
            "session_id must not be empty");
    }

    // Check that upstream identity references are non-empty
    if (manifest.durable_store_import_provider_conformance_report_identity.empty() ||
        manifest.durable_store_import_provider_schema_compatibility_report_identity.empty() ||
        manifest.durable_store_import_provider_config_bundle_validation_report_identity.empty() ||
        manifest.durable_store_import_provider_production_readiness_evidence_identity.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest upstream evidence identities must not be empty");
    }

    // Check that archive_summary is non-empty
    if (manifest.archive_summary.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics, "release evidence archive manifest archive_summary must not be empty");
    }

    // Check that counts are non-negative
    if (manifest.total_evidence_count < 0 || manifest.present_evidence_count < 0 ||
        manifest.valid_evidence_count < 0 || manifest.missing_evidence_count < 0 ||
        manifest.invalid_evidence_count < 0) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics, "release evidence archive manifest counts cannot be negative");
    }

    // Check count consistency
    if (manifest.total_evidence_count !=
        manifest.present_evidence_count + manifest.missing_evidence_count) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest total_evidence_count must equal present + missing");
    }

    return result;
}

ReleaseEvidenceArchiveManifestResult build_release_evidence_archive_manifest(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderProductionReadinessEvidence &readiness_evidence) {
    ReleaseEvidenceArchiveManifestResult result;

    // Validate basic availability of upstream artifacts
    result.diagnostics.append(validate_provider_conformance_report(conformance_report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ReleaseEvidenceArchiveManifest manifest;
    manifest.workflow_canonical_name = conformance_report.workflow_canonical_name;
    manifest.session_id = conformance_report.session_id;
    manifest.run_id = conformance_report.run_id;

    // Set upstream artifact identity references
    manifest.durable_store_import_provider_conformance_report_identity =
        conformance_report.durable_store_import_provider_conformance_report_identity;
    manifest.durable_store_import_provider_schema_compatibility_report_identity =
        "schema-compatibility-report::" + schema_report.session_id;
    manifest.durable_store_import_provider_config_bundle_validation_report_identity =
        config_report.config_bundle_identity;
    manifest.durable_store_import_provider_production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // Build evidence items
    std::vector<EvidenceItem> items;
    items.push_back(provider_release_evidence_archive_detail_make_conformance_evidence_item(
        conformance_report));
    items.push_back(
        provider_release_evidence_archive_detail_make_schema_compatibility_evidence_item(
            schema_report));
    items.push_back(provider_release_evidence_archive_detail_make_config_validation_evidence_item(
        config_report));
    items.push_back(
        provider_release_evidence_archive_detail_make_readiness_evidence_item(readiness_evidence));

    // Compute statistics
    int present_count = 0;
    int valid_count = 0;
    int missing_count = 0;
    int invalid_count = 0;
    for (const auto &item : items) {
        if (item.is_present) {
            ++present_count;
            if (item.is_valid) {
                ++valid_count;
            } else {
                ++invalid_count;
            }
        } else {
            ++missing_count;
        }
    }

    manifest.total_evidence_count = static_cast<int>(items.size());
    manifest.present_evidence_count = present_count;
    manifest.valid_evidence_count = valid_count;
    manifest.missing_evidence_count = missing_count;
    manifest.invalid_evidence_count = invalid_count;
    manifest.evidence_items = std::move(items);

    // Generate aggregate summary
    manifest.is_release_ready =
        (missing_count == 0 && invalid_count == 0 && valid_count == manifest.total_evidence_count);

    if (manifest.is_release_ready) {
        manifest.archive_summary = "all evidence present and valid; release ready";
    } else {
        manifest.archive_summary = "release not ready: " + std::to_string(missing_count) +
                                   " missing, " + std::to_string(invalid_count) + " invalid of " +
                                   std::to_string(manifest.total_evidence_count) +
                                   " total evidence items";
    }

    // Missing evidence summary
    if (missing_count > 0) {
        std::string missing_details;
        for (const auto &item : manifest.evidence_items) {
            if (!item.is_present) {
                if (!missing_details.empty()) {
                    missing_details += ", ";
                }
                missing_details += item.evidence_type;
            }
        }
        manifest.missing_evidence_summary = "missing: " + missing_details;
    } else {
        manifest.missing_evidence_summary = "none";
    }

    // Stale evidence summary (deterministic placeholder)
    manifest.stale_evidence_summary = "none";

    // Incompatible evidence summary
    if (invalid_count > 0) {
        std::string incompatible_details;
        for (const auto &item : manifest.evidence_items) {
            if (item.is_present && !item.is_valid) {
                if (!incompatible_details.empty()) {
                    incompatible_details += ", ";
                }
                incompatible_details += item.evidence_type;
            }
        }
        manifest.incompatible_evidence_summary = "invalid: " + incompatible_details;
    } else {
        manifest.incompatible_evidence_summary = "none";
    }

    // Validate the generated manifest
    result.diagnostics.append(validate_release_evidence_archive_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.manifest = std::move(manifest);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_approval_workflow.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/approval_workflow.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_approval_workflow_detail_kValidationDiagnosticCode{"DSI_PROVIDER_APPROVAL_WORKFLOW"};

void provider_approval_workflow_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                             std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_approval_workflow_detail_kValidationDiagnosticCode, message);
}

} // namespace

std::string_view to_string_view(ApprovalDecision decision) {
    switch (decision) {
    case ApprovalDecision::Approved:
        return "Approved";
    case ApprovalDecision::Rejected:
        return "Rejected";
    case ApprovalDecision::Deferred:
        return "Deferred";
    }
    return "Rejected";
}

ApprovalRequestValidationResult validate_approval_request(const ApprovalRequest &request) {
    ApprovalRequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (request.format_version != kProviderApprovalRequestFormatVersion) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval request format_version must be '" +
                std::string(kProviderApprovalRequestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty() || request.session_id.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval request workflow_canonical_name and session_id must not be empty");
    }

    if (request.release_evidence_archive_manifest_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval request release_evidence_archive_manifest_identity must not be empty");
    }

    if (request.production_readiness_evidence_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval request production_readiness_evidence_identity must not be empty");
    }

    if (request.requestor_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval request requestor_identity must not be empty");
    }

    if (request.approval_scope.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval request approval_scope must not be empty");
    }

    if (request.request_justification.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval request request_justification must not be empty");
    }

    return result;
}

ApprovalDecisionRecordValidationResult
validate_approval_decision_record(const ApprovalDecisionRecord &decision) {
    ApprovalDecisionRecordValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (decision.format_version != kProviderApprovalDecisionFormatVersion) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval decision format_version must be '" +
                std::string(kProviderApprovalDecisionFormatVersion) + "'");
    }

    if (decision.approval_request_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval decision approval_request_identity must not be empty");
    }

    if (decision.decision_reason.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval decision decision_reason must not be empty");
    }

    // Rejection details are required when the decision is Rejected
    if (decision.decision == ApprovalDecision::Rejected &&
        !decision.rejection_details.has_value()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval decision rejection_details required when decision is Rejected");
    }

    return result;
}

ApprovalReceiptValidationResult validate_approval_receipt(const ApprovalReceipt &receipt) {
    ApprovalReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kProviderApprovalReceiptFormatVersion) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval receipt format_version must be '" +
                std::string(kProviderApprovalReceiptFormatVersion) + "'");
    }

    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval receipt workflow_canonical_name and session_id must not be empty");
    }

    if (receipt.approval_request_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval receipt approval_request_identity must not be empty");
    }

    if (receipt.approval_decision_identity.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval receipt approval_decision_identity must not be empty");
    }

    if (receipt.receipt_summary.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval receipt receipt_summary must not be empty");
    }

    // is_approved can only be true when the decision is Approved
    if (receipt.is_approved && receipt.final_decision != ApprovalDecision::Approved) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval receipt is_approved must be false when final_decision is not Approved");
    }

    // blocking_reason_summary is required when the decision is not Approved
    if (receipt.final_decision != ApprovalDecision::Approved &&
        receipt.blocking_reason_summary.empty()) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics, "approval receipt blocking_reason_summary required when not approved");
    }

    return result;
}

ApprovalRequestResult build_approval_request(const ReleaseEvidenceArchiveManifest &archive,
                                             const ProviderProductionReadinessEvidence &readiness) {
    ApprovalRequestResult result;

    // Validate basic availability of upstream artifacts
    result.diagnostics.append(validate_release_evidence_archive_manifest(archive).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.diagnostics.append(
        validate_provider_production_readiness_evidence(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ApprovalRequest request;
    request.workflow_canonical_name = archive.workflow_canonical_name;
    request.session_id = archive.session_id;
    request.run_id = archive.run_id;

    // Bind upstream evidence identities
    request.release_evidence_archive_manifest_identity =
        "release-evidence-archive::" + archive.session_id;
    request.production_readiness_evidence_identity =
        readiness.durable_store_import_provider_production_readiness_evidence_identity;

    // Set default approval request values (real values are determined by the organization process)
    request.requestor_identity = "ahfl-compiler-toolchain";
    request.approval_scope = "provider-production-integration";
    request.request_justification =
        "release evidence archive and production readiness evidence available for review";

    // Validate the generated request
    result.diagnostics.append(validate_approval_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

ApprovalReceiptResult build_approval_receipt(const ApprovalRequest &request,
                                             const ApprovalDecisionRecord &decision) {
    ApprovalReceiptResult result;

    // Validate upstream artifacts
    result.diagnostics.append(validate_approval_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.diagnostics.append(validate_approval_decision_record(decision).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ApprovalReceipt receipt;
    receipt.workflow_canonical_name = request.workflow_canonical_name;
    receipt.session_id = request.session_id;
    receipt.run_id = request.run_id;

    // Bind upstream artifact identities
    receipt.approval_request_identity = "approval-request::" + request.session_id;
    receipt.approval_decision_identity = "approval-decision::" + decision.approval_request_identity;

    // Propagate the final decision
    receipt.final_decision = decision.decision;
    // Safe default: set is_approved = true only when the decision is Approved
    receipt.is_approved = (decision.decision == ApprovalDecision::Approved);

    // Evidence binding
    receipt.release_evidence_archive_manifest_identity =
        request.release_evidence_archive_manifest_identity;
    receipt.production_readiness_evidence_identity = request.production_readiness_evidence_identity;

    // Generate aggregate summary
    switch (decision.decision) {
    case ApprovalDecision::Approved:
        receipt.receipt_summary = "production integration approved; " + decision.decision_reason;
        receipt.blocking_reason_summary = "none";
        break;
    case ApprovalDecision::Rejected:
        receipt.receipt_summary = "production integration rejected; " + decision.decision_reason;
        receipt.blocking_reason_summary =
            decision.rejection_details.value_or("rejection details not provided");
        break;
    case ApprovalDecision::Deferred:
        receipt.receipt_summary = "production integration deferred; " + decision.decision_reason;
        receipt.blocking_reason_summary = "deferred: " + decision.decision_reason;
        break;
    }

    // Validate the generated receipt
    result.diagnostics.append(validate_approval_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_opt_in_guard.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/opt_in_guard.hpp"

#include <string>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(OptInDecision decision) {
    switch (decision) {
    case OptInDecision::Allow:
        return "Allow";
    case OptInDecision::Deny:
        return "Deny";
    case OptInDecision::DenyWithOverride:
        return "DenyWithOverride";
    }
    return "Deny";
}

[[nodiscard]] std::string_view to_string_view(OptInDenialReason reason) {
    switch (reason) {
    case OptInDenialReason::NoApproval:
        return "NoApproval";
    case OptInDenialReason::ConfigInvalid:
        return "ConfigInvalid";
    case OptInDenialReason::RegistryMismatch:
        return "RegistryMismatch";
    case OptInDenialReason::ReadinessNotMet:
        return "ReadinessNotMet";
    case OptInDenialReason::ExplicitDeny:
        return "ExplicitDeny";
    case OptInDenialReason::ScopeExceeded:
        return "ScopeExceeded";
    }
    return "ExplicitDeny";
}

[[nodiscard]] ProviderOptInDecisionReportValidationResult
validate_provider_opt_in_decision_report(const ProviderOptInDecisionReport &report) {
    DiagnosticBag diagnostics;

    // Validate format_version
    if (report.format_version != kProviderOptInDecisionReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // Validate required fields
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // Validate consistency between decision and is_real_provider_traffic_allowed
    if (report.decision != OptInDecision::Allow && report.is_real_provider_traffic_allowed) {
        diagnostics.error()
            .message("is_real_provider_traffic_allowed=true is inconsistent with decision=" +
                     std::string(to_string_view(report.decision)))
            .emit();
    }
    if (report.decision == OptInDecision::Allow && !report.is_real_provider_traffic_allowed) {
        diagnostics.error()
            .message("is_real_provider_traffic_allowed=false is inconsistent with decision=Allow")
            .emit();
    }

    // Validate gates count consistency
    int actual_passed = 0;
    int actual_failed = 0;
    for (const auto &gate : report.gate_checks) {
        if (gate.passed) {
            ++actual_passed;
        } else {
            ++actual_failed;
        }
    }
    if (actual_passed != report.gates_passed) {
        diagnostics.error().message("gates_passed count mismatch").emit();
    }
    if (actual_failed != report.gates_failed) {
        diagnostics.error().message("gates_failed count mismatch").emit();
    }

    // Validate that denial_reasons is non-empty when decision != Allow
    if (report.decision != OptInDecision::Allow && report.denial_reasons.empty()) {
        diagnostics.error()
            .message("denial_reasons must not be empty when decision is not Allow")
            .emit();
    }

    return ProviderOptInDecisionReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderOptInDecisionReportResult
build_provider_opt_in_decision_report(const ApprovalReceipt &approval_receipt,
                                      const ProviderConfigBundleValidationReport &config_report,
                                      const ProviderSelectionPlan &selection_plan) {

    DiagnosticBag diagnostics;
    ProviderOptInDecisionReport report;

    // Fill in base information
    report.workflow_canonical_name = approval_receipt.workflow_canonical_name;
    report.session_id = approval_receipt.session_id;
    report.run_id = approval_receipt.run_id;

    // Fill in upstream identity references
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;

    // Gate 1: Approval gate - check whether approval was granted
    OptInGateCheck approval_gate;
    approval_gate.gate_name = "approval-receipt";
    approval_gate.evidence_reference = approval_receipt.approval_decision_identity;
    if (approval_receipt.is_approved &&
        approval_receipt.final_decision == ApprovalDecision::Approved) {
        approval_gate.passed = true;
    } else {
        approval_gate.passed = false;
        approval_gate.denial_reason = OptInDenialReason::NoApproval;
    }
    report.gate_checks.push_back(std::move(approval_gate));

    // Gate 2: Config validation gate - check whether the configuration is valid
    OptInGateCheck config_gate;
    config_gate.gate_name = "config-bundle-validation";
    config_gate.evidence_reference = config_report.config_bundle_identity;
    if (config_report.invalid_count == 0 && config_report.missing_count == 0) {
        config_gate.passed = true;
    } else {
        config_gate.passed = false;
        config_gate.denial_reason = OptInDenialReason::ConfigInvalid;
    }
    report.gate_checks.push_back(std::move(config_gate));

    // Gate 3: Registry selection gate - check provider registry and selection match
    OptInGateCheck registry_gate;
    registry_gate.gate_name = "registry-selection-plan";
    registry_gate.evidence_reference =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    if (selection_plan.selection_status == ProviderSelectionStatus::Selected ||
        selection_plan.selection_status == ProviderSelectionStatus::FallbackSelected) {
        registry_gate.passed = true;
    } else {
        registry_gate.passed = false;
        registry_gate.denial_reason = OptInDenialReason::RegistryMismatch;
    }
    report.gate_checks.push_back(std::move(registry_gate));

    // Gate 4: Security constraints gate - ensure config performs no sensitive operations
    OptInGateCheck security_gate;
    security_gate.gate_name = "security-constraints";
    security_gate.evidence_reference = config_report.config_bundle_identity;
    if (!config_report.reads_secret_value && !config_report.opens_network_connection) {
        security_gate.passed = true;
    } else {
        security_gate.passed = false;
        security_gate.denial_reason = OptInDenialReason::ReadinessNotMet;
    }
    report.gate_checks.push_back(std::move(security_gate));

    // Aggregate gate results
    for (const auto &gate : report.gate_checks) {
        if (gate.passed) {
            ++report.gates_passed;
        } else {
            ++report.gates_failed;
            if (gate.denial_reason.has_value()) {
                report.denial_reasons.push_back(*gate.denial_reason);
            }
        }
    }

    // Decision logic: Allow when all gates pass, otherwise Deny
    if (report.gates_failed == 0) {
        report.decision = OptInDecision::Allow;
        report.is_real_provider_traffic_allowed = true;
        report.decision_summary = "all gates passed; real provider traffic allowed";
        report.denial_reason_summary = "none";
    } else {
        report.decision = OptInDecision::Deny;
        report.is_real_provider_traffic_allowed = false;
        report.decision_summary = "one or more gates failed; real provider traffic denied";

        // Build the denial reason summary
        std::string reasons;
        for (const auto &reason : report.denial_reasons) {
            if (!reasons.empty()) {
                reasons += ", ";
            }
            reasons += to_string_view(reason);
        }
        report.denial_reason_summary = reasons;
    }

    return ProviderOptInDecisionReportResult{std::move(report), std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

// ---- provider_runtime_policy.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/runtime_policy.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(PolicyDecision decision) {
    switch (decision) {
    case PolicyDecision::Permit:
        return "Permit";
    case PolicyDecision::Deny:
        return "Deny";
    case PolicyDecision::DenyWithWarning:
        return "DenyWithWarning";
    }
    return "Deny";
}

[[nodiscard]] std::string_view to_string_view(PolicyViolationType violation) {
    switch (violation) {
    case PolicyViolationType::OptInNotGranted:
        return "OptInNotGranted";
    case PolicyViolationType::ApprovalMissing:
        return "ApprovalMissing";
    case PolicyViolationType::ConfigInvalid:
        return "ConfigInvalid";
    case PolicyViolationType::RegistryMismatch:
        return "RegistryMismatch";
    case PolicyViolationType::ReadinessNotMet:
        return "ReadinessNotMet";
    case PolicyViolationType::ScopeExceeded:
        return "ScopeExceeded";
    case PolicyViolationType::EvidenceStale:
        return "EvidenceStale";
    }
    return "OptInNotGranted";
}

[[nodiscard]] ProviderRuntimePolicyReportValidationResult
validate_provider_runtime_policy_report(const ProviderRuntimePolicyReport &report) {
    DiagnosticBag diagnostics;

    // Validate format_version
    if (report.format_version != kProviderRuntimePolicyReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // Validate required fields
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // Validate consistency between decision and is_execution_permitted
    if (report.decision != PolicyDecision::Permit && report.is_execution_permitted) {
        diagnostics.error()
            .message("is_execution_permitted=true is inconsistent with decision=" +
                     std::string(to_string_view(report.decision)))
            .emit();
    }
    if (report.decision == PolicyDecision::Permit && !report.is_execution_permitted) {
        diagnostics.error()
            .message("is_execution_permitted=false is inconsistent with decision=Permit")
            .emit();
    }

    // Validate gates count consistency
    int actual_passed = 0;
    int actual_failed = 0;
    for (const auto &gate : report.policy_gates) {
        if (gate.passed) {
            ++actual_passed;
        } else {
            ++actual_failed;
        }
    }
    if (actual_passed != report.gates_passed) {
        diagnostics.error().message("gates_passed count mismatch").emit();
    }
    if (actual_failed != report.gates_failed) {
        diagnostics.error().message("gates_failed count mismatch").emit();
    }

    // Validate violation count consistency
    int actual_blocking = 0;
    for (const auto &gate : report.policy_gates) {
        if (!gate.passed) {
            actual_blocking += static_cast<int>(gate.violations.size());
        }
    }
    if (actual_blocking != report.blocking_violation_count) {
        diagnostics.error().message("blocking_violation_count mismatch").emit();
    }

    return ProviderRuntimePolicyReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderRuntimePolicyReportResult build_provider_runtime_policy_report(
    const ProviderOptInDecisionReport &opt_in_report,
    const ApprovalReceipt &approval_receipt,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderSelectionPlan &selection_plan,
    const ProviderProductionReadinessEvidence &readiness_evidence) {

    DiagnosticBag diagnostics;
    ProviderRuntimePolicyReport report;

    // Fill in base information (inherit session info from the opt-in report)
    report.workflow_canonical_name = opt_in_report.workflow_canonical_name;
    report.session_id = opt_in_report.session_id;
    report.run_id = opt_in_report.run_id;

    // Fill in upstream identity references
    report.opt_in_decision_report_identity = "opt-in-decision-report::" + opt_in_report.session_id;
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    report.production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // Gate 1: Opt-In gate - check whether the opt-in decision is Allow
    PolicyGateResult opt_in_gate;
    opt_in_gate.gate_name = "opt_in";
    opt_in_gate.evidence_reference = report.opt_in_decision_report_identity;
    if (opt_in_report.decision == OptInDecision::Allow &&
        opt_in_report.is_real_provider_traffic_allowed) {
        opt_in_gate.passed = true;
    } else {
        opt_in_gate.passed = false;
        opt_in_gate.violations.push_back(PolicyViolationType::OptInNotGranted);
    }
    report.policy_gates.push_back(std::move(opt_in_gate));

    // Gate 2: Approval gate - check whether approval was granted
    PolicyGateResult approval_gate;
    approval_gate.gate_name = "approval";
    approval_gate.evidence_reference = approval_receipt.approval_decision_identity;
    if (approval_receipt.is_approved &&
        approval_receipt.final_decision == ApprovalDecision::Approved) {
        approval_gate.passed = true;
    } else {
        approval_gate.passed = false;
        approval_gate.violations.push_back(PolicyViolationType::ApprovalMissing);
    }
    report.policy_gates.push_back(std::move(approval_gate));

    // Gate 3: Config validation gate - check whether the configuration is valid
    PolicyGateResult config_gate;
    config_gate.gate_name = "config";
    config_gate.evidence_reference = config_report.config_bundle_identity;
    if (config_report.invalid_count == 0 && config_report.missing_count == 0) {
        config_gate.passed = true;
    } else {
        config_gate.passed = false;
        config_gate.violations.push_back(PolicyViolationType::ConfigInvalid);
    }
    report.policy_gates.push_back(std::move(config_gate));

    // Gate 4: Registry selection gate - check provider registry and selection match
    PolicyGateResult registry_gate;
    registry_gate.gate_name = "registry";
    registry_gate.evidence_reference =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    if (selection_plan.selection_status == ProviderSelectionStatus::Selected ||
        selection_plan.selection_status == ProviderSelectionStatus::FallbackSelected) {
        registry_gate.passed = true;
    } else {
        registry_gate.passed = false;
        registry_gate.violations.push_back(PolicyViolationType::RegistryMismatch);
    }
    report.policy_gates.push_back(std::move(registry_gate));

    // Gate 5: Production readiness gate - check readiness evidence
    PolicyGateResult readiness_gate;
    readiness_gate.gate_name = "readiness";
    readiness_gate.evidence_reference =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;
    if (readiness_evidence.blocking_issue_count == 0 &&
        readiness_evidence.security_evidence_passed &&
        readiness_evidence.recovery_evidence_passed &&
        readiness_evidence.compatibility_evidence_passed) {
        readiness_gate.passed = true;
    } else {
        readiness_gate.passed = false;
        readiness_gate.violations.push_back(PolicyViolationType::ReadinessNotMet);
    }
    report.policy_gates.push_back(std::move(readiness_gate));

    // Aggregate gate results
    for (const auto &gate : report.policy_gates) {
        if (gate.passed) {
            ++report.gates_passed;
        } else {
            ++report.gates_failed;
            report.blocking_violation_count += static_cast<int>(gate.violations.size());
        }
    }

    // Decision logic: Permit when all gates pass, otherwise Deny
    if (report.gates_failed == 0) {
        report.decision = PolicyDecision::Permit;
        report.is_execution_permitted = true;
        report.policy_summary = "all policy gates passed; execution permitted";
        report.violation_summary = "none";
        report.next_operator_action = "proceed with provider execution";
    } else {
        report.decision = PolicyDecision::Deny;
        report.is_execution_permitted = false;
        report.policy_summary = "one or more policy gates failed; execution denied";

        // Build the violation summary
        std::string violations;
        for (const auto &gate : report.policy_gates) {
            if (!gate.passed) {
                for (const auto &v : gate.violations) {
                    if (!violations.empty()) {
                        violations += ", ";
                    }
                    violations += to_string_view(v);
                }
            }
        }
        report.violation_summary = violations;
        report.next_operator_action = "resolve policy violations before retrying";
    }

    return ProviderRuntimePolicyReportResult{std::move(report), std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

// ---- provider_production_integration_dry_run.cpp ----

#include "pipeline/persistence/durable_store_import/provider/production/production_integration_dry_run.hpp"

#include <initializer_list>
#include <string>
#include <utility>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(ProductionReadinessState state) {
    switch (state) {
    case ProductionReadinessState::ReadyForControlledRollout:
        return "ReadyForControlledRollout";
    case ProductionReadinessState::ReadyWithConditions:
        return "ReadyWithConditions";
    case ProductionReadinessState::Blocked:
        return "Blocked";
    case ProductionReadinessState::InsufficientEvidence:
        return "InsufficientEvidence";
    }
    return "Blocked";
}

namespace {

struct ProductionIntegrationEvidenceSpec {
    std::string evidence_type;
    std::string evidence_identity;
    std::string format_version;
    bool is_valid{false};
};

[[nodiscard]] EvidenceChainItem provider_production_integration_detail_make_evidence_chain_item(
    const ProductionIntegrationEvidenceSpec &spec) {
    const bool is_present = !spec.evidence_identity.empty() && !spec.format_version.empty();
    return EvidenceChainItem{
        .evidence_type = spec.evidence_type,
        .evidence_identity = spec.evidence_identity,
        .format_version = spec.format_version,
        .is_present = is_present,
        .is_valid = is_present && spec.is_valid,
        .is_fresh = true,
    };
}

void provider_production_integration_detail_summarize_evidence_chain(
    ProviderProductionIntegrationDryRunReport &report) {
    report.total_evidence_count = static_cast<int>(report.evidence_chain.size());
    report.valid_evidence_count = 0;
    report.invalid_evidence_count = 0;
    report.missing_evidence_count = 0;
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            ++report.missing_evidence_count;
        } else if (!item.is_valid) {
            ++report.invalid_evidence_count;
        } else {
            ++report.valid_evidence_count;
        }
    }
}

[[nodiscard]] BlockingItem
provider_production_integration_detail_make_blocking_item(const EvidenceChainItem &item) {
    if (!item.is_present) {
        return BlockingItem{
            .block_type = "missing_evidence",
            .block_reason = item.evidence_type + " evidence is missing",
            .responsible_artifact = item.evidence_identity,
            .suggested_action = "generate " + item.evidence_type + " report",
        };
    }
    return BlockingItem{
        .block_type = "invalid_evidence",
        .block_reason = item.evidence_type + " evidence is invalid",
        .responsible_artifact = item.evidence_identity,
        .suggested_action = "resolve " + item.evidence_type + " failures",
    };
}

void provider_production_integration_detail_rebuild_blocking_items(
    ProviderProductionIntegrationDryRunReport &report) {
    report.blocking_items.clear();
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present || !item.is_valid) {
            report.blocking_items.push_back(
                provider_production_integration_detail_make_blocking_item(item));
        }
    }
    report.blocking_item_count = static_cast<int>(report.blocking_items.size());
}

void provider_production_integration_detail_assign_evidence_chain(
    ProviderProductionIntegrationDryRunReport &report,
    std::initializer_list<ProductionIntegrationEvidenceSpec> specs) {
    report.evidence_chain.clear();
    for (const auto &spec : specs) {
        report.evidence_chain.push_back(
            provider_production_integration_detail_make_evidence_chain_item(spec));
    }
    provider_production_integration_detail_summarize_evidence_chain(report);
    provider_production_integration_detail_rebuild_blocking_items(report);
}

} // namespace

[[nodiscard]] ProviderProductionIntegrationDryRunReportValidationResult
validate_provider_production_integration_dry_run_report(
    const ProviderProductionIntegrationDryRunReport &report) {
    DiagnosticBag diagnostics;

    // Validate format_version
    if (report.format_version != kProviderProductionIntegrationDryRunReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // Validate required fields
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // Validate consistency between readiness_state and is_ready_for_controlled_rollout
    if (report.readiness_state != ProductionReadinessState::ReadyForControlledRollout &&
        report.is_ready_for_controlled_rollout) {
        diagnostics.error()
            .message("is_ready_for_controlled_rollout=true is inconsistent with readiness_state=" +
                     std::string(to_string_view(report.readiness_state)))
            .emit();
    }
    if (report.readiness_state == ProductionReadinessState::ReadyForControlledRollout &&
        !report.is_ready_for_controlled_rollout) {
        diagnostics.error()
            .message("is_ready_for_controlled_rollout=false is inconsistent with "
                     "readiness_state=ReadyForControlledRollout")
            .emit();
    }

    // Validate that is_non_mutating_mode must be true (current version constraint)
    if (!report.is_non_mutating_mode) {
        diagnostics.error().message("is_non_mutating_mode must be true in current version").emit();
    }

    // Validate evidence chain count consistency
    int actual_valid = 0;
    int actual_invalid = 0;
    int actual_missing = 0;
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            ++actual_missing;
        } else if (!item.is_valid) {
            ++actual_invalid;
        } else {
            ++actual_valid;
        }
    }
    if (actual_valid != report.valid_evidence_count) {
        diagnostics.error().message("valid_evidence_count mismatch").emit();
    }
    if (actual_invalid != report.invalid_evidence_count) {
        diagnostics.error().message("invalid_evidence_count mismatch").emit();
    }
    if (actual_missing != report.missing_evidence_count) {
        diagnostics.error().message("missing_evidence_count mismatch").emit();
    }

    // Validate blocking_item_count consistency
    if (static_cast<int>(report.blocking_items.size()) != report.blocking_item_count) {
        diagnostics.error().message("blocking_item_count mismatch").emit();
    }

    return ProviderProductionIntegrationDryRunReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderProductionIntegrationDryRunReportResult
build_provider_production_integration_dry_run_report(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ReleaseEvidenceArchiveManifest &evidence_archive,
    const ApprovalReceipt &approval_receipt,
    const ProviderOptInDecisionReport &opt_in_report,
    const ProviderRuntimePolicyReport &runtime_policy_report) {

    DiagnosticBag diagnostics;
    ProviderProductionIntegrationDryRunReport report;

    // Fill in base information (inherit session info from the conformance report)
    report.workflow_canonical_name = conformance_report.workflow_canonical_name;
    report.session_id = conformance_report.session_id;
    report.run_id = conformance_report.run_id;

    // Fill in evidence chain identity references
    report.conformance_report_identity =
        conformance_report.durable_store_import_provider_conformance_report_identity;
    report.schema_compatibility_report_identity =
        "schema-compatibility-report::" + schema_report.session_id;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.release_evidence_archive_manifest_identity =
        evidence_archive.durable_store_import_provider_conformance_report_identity;
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.opt_in_decision_report_identity = "opt-in-decision-report::" + opt_in_report.session_id;
    report.runtime_policy_report_identity =
        "runtime-policy-report::" + runtime_policy_report.session_id;

    provider_production_integration_detail_assign_evidence_chain(
        report,
        {
            {"conformance",
             report.conformance_report_identity,
             conformance_report.format_version,
             conformance_report.fail_count == 0},
            {"schema_compatibility",
             report.schema_compatibility_report_identity,
             schema_report.format_version,
             schema_report.incompatible_count == 0 && !schema_report.has_schema_drift},
            {"config_validation",
             report.config_validation_report_identity,
             config_report.format_version,
             config_report.invalid_count == 0 && config_report.missing_count == 0},
            {"release_archive",
             report.release_evidence_archive_manifest_identity,
             evidence_archive.format_version,
             evidence_archive.is_release_ready},
            {"approval",
             report.approval_receipt_identity,
             approval_receipt.format_version,
             approval_receipt.is_approved},
            {"opt_in",
             report.opt_in_decision_report_identity,
             opt_in_report.format_version,
             opt_in_report.is_real_provider_traffic_allowed},
            {"runtime_policy",
             report.runtime_policy_report_identity,
             runtime_policy_report.format_version,
             runtime_policy_report.is_execution_permitted},
        });

    // ===== Determine readiness state =====
    if (report.missing_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::InsufficientEvidence;
    } else if (report.invalid_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::Blocked;
    } else {
        report.readiness_state = ProductionReadinessState::ReadyForControlledRollout;
    }

    // ===== Set is_ready_for_controlled_rollout =====
    report.is_ready_for_controlled_rollout =
        (report.readiness_state == ProductionReadinessState::ReadyForControlledRollout);

    // ===== Build next operator actions =====
    if (report.is_ready_for_controlled_rollout) {
        NextOperatorAction action;
        action.action_type = "proceed";
        action.action_description = "all evidence passed; proceed with controlled rollout";
        action.action_target = report.workflow_canonical_name;
        action.priority = 1;
        report.next_operator_actions.push_back(std::move(action));
    } else {
        int priority = 1;
        for (const auto &blocker : report.blocking_items) {
            NextOperatorAction action;
            action.action_type = "resolve_blocker";
            action.action_description = blocker.suggested_action;
            action.action_target = blocker.responsible_artifact;
            action.priority = priority++;
            report.next_operator_actions.push_back(std::move(action));
        }
    }

    // ===== Build summary =====
    if (report.is_ready_for_controlled_rollout) {
        report.blocking_summary = "none";
        report.dry_run_summary =
            "all evidence chain items passed; production integration dry run succeeded; "
            "ready for controlled rollout";
    } else {
        std::string blocking_desc;
        for (const auto &blocker : report.blocking_items) {
            if (!blocking_desc.empty()) {
                blocking_desc += "; ";
            }
            blocking_desc += blocker.block_reason;
        }
        report.blocking_summary = blocking_desc;
        report.dry_run_summary = "production integration dry run blocked: " + blocking_desc;
    }

    // non-mutating mode is always true
    report.is_non_mutating_mode = true;

    return ProviderProductionIntegrationDryRunReportResult{std::move(report),
                                                           std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

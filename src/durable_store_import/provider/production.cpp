// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_production_readiness.cpp ----

#include "ahfl/durable_store_import/provider/production/production_readiness.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_production_readiness_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_PRODUCTION_READINESS";

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

#include "ahfl/durable_store_import/provider/production/release_evidence_archive.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view
    provider_release_evidence_archive_detail_kValidationDiagnosticCode =
        "AHFL.VAL.DSI_PROVIDER_RELEASE_EVIDENCE_ARCHIVE";

void provider_release_evidence_archive_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                    std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_release_evidence_archive_detail_kValidationDiagnosticCode, message);
}

// 构建单个 evidence item（从 conformance report）
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

// 构建单个 evidence item（从 schema compatibility report）
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

// 构建单个 evidence item（从 config bundle validation report）
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

// 构建单个 evidence item（从 production readiness evidence）
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

    // 检查 format_version
    if (manifest.format_version != kProviderReleaseEvidenceArchiveManifestFormatVersion) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest format_version must be '" +
                std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion) + "'");
    }

    // 检查必需字段非空
    if (manifest.workflow_canonical_name.empty() || manifest.session_id.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest workflow_canonical_name and "
            "session_id must not be empty");
    }

    // 检查上游 identity 引用非空
    if (manifest.durable_store_import_provider_conformance_report_identity.empty() ||
        manifest.durable_store_import_provider_schema_compatibility_report_identity.empty() ||
        manifest.durable_store_import_provider_config_bundle_validation_report_identity.empty() ||
        manifest.durable_store_import_provider_production_readiness_evidence_identity.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics,
            "release evidence archive manifest upstream evidence identities must not be empty");
    }

    // 检查 archive_summary 非空
    if (manifest.archive_summary.empty()) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics, "release evidence archive manifest archive_summary must not be empty");
    }

    // 检查计数非负
    if (manifest.total_evidence_count < 0 || manifest.present_evidence_count < 0 ||
        manifest.valid_evidence_count < 0 || manifest.missing_evidence_count < 0 ||
        manifest.invalid_evidence_count < 0) {
        provider_release_evidence_archive_detail_emit_validation_error(
            diagnostics, "release evidence archive manifest counts cannot be negative");
    }

    // 检查计数一致性
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

    // 验证上游 artifact 基础可用性
    result.diagnostics.append(validate_provider_conformance_report(conformance_report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ReleaseEvidenceArchiveManifest manifest;
    manifest.workflow_canonical_name = conformance_report.workflow_canonical_name;
    manifest.session_id = conformance_report.session_id;
    manifest.run_id = conformance_report.run_id;

    // 设置上游 artifact identity 引用
    manifest.durable_store_import_provider_conformance_report_identity =
        conformance_report.durable_store_import_provider_conformance_report_identity;
    manifest.durable_store_import_provider_schema_compatibility_report_identity =
        "schema-compatibility-report::" + schema_report.session_id;
    manifest.durable_store_import_provider_config_bundle_validation_report_identity =
        config_report.config_bundle_identity;
    manifest.durable_store_import_provider_production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // 构建 evidence items
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

    // 计算统计
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

    // 生成汇总摘要
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

    // 验证生成的 manifest
    result.diagnostics.append(validate_release_evidence_archive_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.manifest = std::move(manifest);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_approval_workflow.cpp ----

#include "ahfl/durable_store_import/provider/production/approval_workflow.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_approval_workflow_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_APPROVAL_WORKFLOW";

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

    // 拒绝时必须有拒绝详情
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

    // is_approved 只能在 Approved 决定时为 true
    if (receipt.is_approved && receipt.final_decision != ApprovalDecision::Approved) {
        provider_approval_workflow_detail_emit_validation_error(
            diagnostics,
            "approval receipt is_approved must be false when final_decision is not Approved");
    }

    // 非 Approved 时必须有 blocking_reason_summary
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

    // 验证上游 artifact 基础可用性
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

    // 绑定上游 evidence identity
    request.release_evidence_archive_manifest_identity =
        "release-evidence-archive::" + archive.session_id;
    request.production_readiness_evidence_identity =
        readiness.durable_store_import_provider_production_readiness_evidence_identity;

    // 设置审批请求默认值（由组织流程决定真实值）
    request.requestor_identity = "ahfl-compiler-toolchain";
    request.approval_scope = "provider-production-integration";
    request.request_justification =
        "release evidence archive and production readiness evidence available for review";

    // 验证生成的 request
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

    // 验证上游 artifact
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

    // 绑定上游 artifact identity
    receipt.approval_request_identity = "approval-request::" + request.session_id;
    receipt.approval_decision_identity = "approval-decision::" + decision.approval_request_identity;

    // 传递最终决定
    receipt.final_decision = decision.decision;
    // 安全默认：只在 Approved 决定时设置 is_approved = true
    receipt.is_approved = (decision.decision == ApprovalDecision::Approved);

    // Evidence binding
    receipt.release_evidence_archive_manifest_identity =
        request.release_evidence_archive_manifest_identity;
    receipt.production_readiness_evidence_identity = request.production_readiness_evidence_identity;

    // 生成汇总摘要
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

    // 验证生成的 receipt
    result.diagnostics.append(validate_approval_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_opt_in_guard.cpp ----

#include "ahfl/durable_store_import/provider/production/opt_in_guard.hpp"

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

    // 校验 format_version
    if (report.format_version != kProviderOptInDecisionReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 decision 与 is_real_provider_traffic_allowed 一致性
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

    // 校验 gates 计数一致性
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

    // 校验 denial_reasons 非空 when decision != Allow
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

    // 填充基础信息
    report.workflow_canonical_name = approval_receipt.workflow_canonical_name;
    report.session_id = approval_receipt.session_id;
    report.run_id = approval_receipt.run_id;

    // 填充上游 identity 引用
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;

    // Gate 1: Approval gate - 检查审批是否通过
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

    // Gate 2: Config validation gate - 检查配置是否有效
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

    // Gate 3: Registry selection gate - 检查 provider 注册和选择是否匹配
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

    // Gate 4: Security constraints gate - 确保 config 不涉及敏感操作
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

    // 汇总 gates 结果
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

    // 决定逻辑：所有 gate 通过时 Allow，否则 Deny
    if (report.gates_failed == 0) {
        report.decision = OptInDecision::Allow;
        report.is_real_provider_traffic_allowed = true;
        report.decision_summary = "all gates passed; real provider traffic allowed";
        report.denial_reason_summary = "none";
    } else {
        report.decision = OptInDecision::Deny;
        report.is_real_provider_traffic_allowed = false;
        report.decision_summary = "one or more gates failed; real provider traffic denied";

        // 构建 denial reason summary
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

#include "ahfl/durable_store_import/provider/production/runtime_policy.hpp"

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

    // 校验 format_version
    if (report.format_version != kProviderRuntimePolicyReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 decision 与 is_execution_permitted 一致性
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

    // 校验 gates 计数一致性
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

    // 校验 violation 计数一致性
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

    // 填充基础信息（从 opt-in report 继承 session 信息）
    report.workflow_canonical_name = opt_in_report.workflow_canonical_name;
    report.session_id = opt_in_report.session_id;
    report.run_id = opt_in_report.run_id;

    // 填充上游 identity 引用
    report.opt_in_decision_report_identity = "opt-in-decision-report::" + opt_in_report.session_id;
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    report.production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // Gate 1: Opt-In gate - 检查 opt-in decision 是否为 Allow
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

    // Gate 2: Approval gate - 检查审批是否通过
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

    // Gate 3: Config validation gate - 检查配置是否有效
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

    // Gate 4: Registry selection gate - 检查 provider 注册和选择是否匹配
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

    // Gate 5: Production readiness gate - 检查就绪证据
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

    // 汇总 gates 结果
    for (const auto &gate : report.policy_gates) {
        if (gate.passed) {
            ++report.gates_passed;
        } else {
            ++report.gates_failed;
            report.blocking_violation_count += static_cast<int>(gate.violations.size());
        }
    }

    // 决定逻辑：所有 gate 通过时 Permit，否则 Deny
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

        // 构建 violation summary
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

#include "ahfl/durable_store_import/provider/production/production_integration_dry_run.hpp"

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

[[nodiscard]] ProviderProductionIntegrationDryRunReportValidationResult
validate_provider_production_integration_dry_run_report(
    const ProviderProductionIntegrationDryRunReport &report) {
    DiagnosticBag diagnostics;

    // 校验 format_version
    if (report.format_version != kProviderProductionIntegrationDryRunReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 readiness_state 与 is_ready_for_controlled_rollout 一致性
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

    // 校验 is_non_mutating_mode 必须为 true（当前版本约束）
    if (!report.is_non_mutating_mode) {
        diagnostics.error().message("is_non_mutating_mode must be true in current version").emit();
    }

    // 校验 evidence chain 计数一致性
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

    // 校验 blocking_item_count 一致性
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

    // 填充基础信息（从 conformance report 继承 session 信息）
    report.workflow_canonical_name = conformance_report.workflow_canonical_name;
    report.session_id = conformance_report.session_id;
    report.run_id = conformance_report.run_id;

    // 填充 evidence chain identity 引用
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

    // ===== 构建 Evidence Chain =====

    // v0.43: Conformance Report
    {
        EvidenceChainItem item;
        item.evidence_type = "conformance";
        item.evidence_identity = report.conformance_report_identity;
        item.format_version = conformance_report.format_version;
        item.is_present = true;
        item.is_valid = (conformance_report.fail_count == 0);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.44: Schema Compatibility Report
    {
        EvidenceChainItem item;
        item.evidence_type = "schema_compatibility";
        item.evidence_identity = report.schema_compatibility_report_identity;
        item.format_version = schema_report.format_version;
        item.is_present = true;
        item.is_valid = (schema_report.incompatible_count == 0 && !schema_report.has_schema_drift);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.45: Config Bundle Validation Report
    {
        EvidenceChainItem item;
        item.evidence_type = "config_validation";
        item.evidence_identity = report.config_validation_report_identity;
        item.format_version = config_report.format_version;
        item.is_present = true;
        item.is_valid = (config_report.invalid_count == 0 && config_report.missing_count == 0);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.46: Release Evidence Archive Manifest
    {
        EvidenceChainItem item;
        item.evidence_type = "release_archive";
        item.evidence_identity = report.release_evidence_archive_manifest_identity;
        item.format_version = evidence_archive.format_version;
        item.is_present = true;
        item.is_valid = evidence_archive.is_release_ready;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.47: Approval Receipt
    {
        EvidenceChainItem item;
        item.evidence_type = "approval";
        item.evidence_identity = report.approval_receipt_identity;
        item.format_version = approval_receipt.format_version;
        item.is_present = true;
        item.is_valid = approval_receipt.is_approved;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.48: Opt-In Decision Report
    {
        EvidenceChainItem item;
        item.evidence_type = "opt_in";
        item.evidence_identity = report.opt_in_decision_report_identity;
        item.format_version = opt_in_report.format_version;
        item.is_present = true;
        item.is_valid = opt_in_report.is_real_provider_traffic_allowed;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.49: Runtime Policy Report
    {
        EvidenceChainItem item;
        item.evidence_type = "runtime_policy";
        item.evidence_identity = report.runtime_policy_report_identity;
        item.format_version = runtime_policy_report.format_version;
        item.is_present = true;
        item.is_valid = runtime_policy_report.is_execution_permitted;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // ===== 汇总 evidence chain 计数 =====
    report.total_evidence_count = static_cast<int>(report.evidence_chain.size());
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            ++report.missing_evidence_count;
        } else if (!item.is_valid) {
            ++report.invalid_evidence_count;
        } else {
            ++report.valid_evidence_count;
        }
    }

    // ===== 构建 blocking items =====
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            BlockingItem blocker;
            blocker.block_type = "missing_evidence";
            blocker.block_reason = item.evidence_type + " evidence is missing";
            blocker.responsible_artifact = item.evidence_identity;
            blocker.suggested_action = "generate " + item.evidence_type + " report";
            report.blocking_items.push_back(std::move(blocker));
        } else if (!item.is_valid) {
            BlockingItem blocker;
            blocker.block_type = "invalid_evidence";
            blocker.block_reason = item.evidence_type + " evidence is invalid";
            blocker.responsible_artifact = item.evidence_identity;
            blocker.suggested_action = "resolve " + item.evidence_type + " failures";
            report.blocking_items.push_back(std::move(blocker));
        }
    }
    report.blocking_item_count = static_cast<int>(report.blocking_items.size());

    // ===== 确定 readiness state =====
    if (report.missing_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::InsufficientEvidence;
    } else if (report.invalid_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::Blocked;
    } else {
        report.readiness_state = ProductionReadinessState::ReadyForControlledRollout;
    }

    // ===== 设定 is_ready_for_controlled_rollout =====
    report.is_ready_for_controlled_rollout =
        (report.readiness_state == ProductionReadinessState::ReadyForControlledRollout);

    // ===== 构建 next operator actions =====
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

    // ===== 构建 summary =====
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

    // non-mutating 模式始终为 true
    report.is_non_mutating_mode = true;

    return ProviderProductionIntegrationDryRunReportResult{std::move(report),
                                                           std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

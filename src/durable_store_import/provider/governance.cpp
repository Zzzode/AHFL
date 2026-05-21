// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_audit.cpp ----

#include "ahfl/durable_store_import/provider_audit.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_audit_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_AUDIT";

void provider_audit_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_audit_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_audit_detail_outcome_slug(ProviderAuditOutcome outcome) {
    switch (outcome) {
    case ProviderAuditOutcome::Success:
        return "success";
    case ProviderAuditOutcome::Failure:
        return "failure";
    case ProviderAuditOutcome::RetryPending:
        return "retry-pending";
    case ProviderAuditOutcome::RecoveryPending:
        return "recovery-pending";
    case ProviderAuditOutcome::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] ProviderAuditOutcome
provider_audit_detail_outcome_for_failure(ProviderFailureKindV1 kind) {
    switch (kind) {
    case ProviderFailureKindV1::None:
        return ProviderAuditOutcome::Success;
    case ProviderFailureKindV1::Network:
    case ProviderFailureKindV1::Throttling:
        return ProviderAuditOutcome::RetryPending;
    case ProviderFailureKindV1::Conflict:
        return ProviderAuditOutcome::RecoveryPending;
    case ProviderFailureKindV1::Authentication:
    case ProviderFailureKindV1::Authorization:
    case ProviderFailureKindV1::SchemaMismatch:
    case ProviderFailureKindV1::ProviderInternal:
    case ProviderFailureKindV1::Unknown:
        return ProviderAuditOutcome::Failure;
    case ProviderFailureKindV1::Blocked:
        return ProviderAuditOutcome::Blocked;
    }
    return ProviderAuditOutcome::Blocked;
}

[[nodiscard]] ProviderAuditNextActionKind
provider_audit_detail_next_action_for_outcome(ProviderAuditOutcome outcome) {
    switch (outcome) {
    case ProviderAuditOutcome::Success:
        return ProviderAuditNextActionKind::Archive;
    case ProviderAuditOutcome::RetryPending:
        return ProviderAuditNextActionKind::RetryWithIdempotency;
    case ProviderAuditOutcome::RecoveryPending:
        return ProviderAuditNextActionKind::RecoveryReview;
    case ProviderAuditOutcome::Failure:
    case ProviderAuditOutcome::Blocked:
        return ProviderAuditNextActionKind::OperatorReview;
    }
    return ProviderAuditNextActionKind::OperatorReview;
}

} // namespace

ProviderExecutionAuditEventValidationResult
validate_provider_execution_audit_event(const ProviderExecutionAuditEvent &event) {
    ProviderExecutionAuditEventValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (event.format_version != kProviderExecutionAuditEventFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider execution audit event format_version must be '" +
                std::string(kProviderExecutionAuditEventFormatVersion) + "'");
    }
    if (event.source_durable_store_import_provider_failure_taxonomy_report_format_version !=
        kProviderFailureTaxonomyReportFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider execution audit event source format_version must be '" +
                std::string(kProviderFailureTaxonomyReportFormatVersion) + "'");
    }
    if (event.workflow_canonical_name.empty() || event.session_id.empty() ||
        event.input_fixture.empty() ||
        event.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        event.durable_store_import_provider_execution_audit_event_identity.empty() ||
        event.event_name.empty() ||
        event.redaction_policy_version != kProviderAuditRedactionPolicyVersion ||
        event.event_summary.empty()) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider execution audit event identity and summary fields must not be empty");
    }
    if (!event.secret_free || event.raw_telemetry_persisted) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider execution audit event must be secret-free and cannot persist raw telemetry");
    }
    return result;
}

ProviderTelemetrySummaryValidationResult
validate_provider_telemetry_summary(const ProviderTelemetrySummary &summary) {
    ProviderTelemetrySummaryValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (summary.format_version != kProviderTelemetrySummaryFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider telemetry summary format_version must be '" +
                std::string(kProviderTelemetrySummaryFormatVersion) + "'");
    }
    if (summary.source_durable_store_import_provider_execution_audit_event_format_version !=
        kProviderExecutionAuditEventFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider telemetry summary source format_version must be '" +
                std::string(kProviderExecutionAuditEventFormatVersion) + "'");
    }
    if (summary.workflow_canonical_name.empty() || summary.session_id.empty() ||
        summary.input_fixture.empty() ||
        summary.durable_store_import_provider_execution_audit_event_identity.empty() ||
        summary.durable_store_import_provider_telemetry_summary_identity.empty() ||
        summary.telemetry_summary.empty()) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider telemetry summary identity and summary fields must not be empty");
    }
    if (!summary.secret_free || summary.raw_telemetry_persisted) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider telemetry summary must be secret-free and cannot persist raw telemetry");
    }
    return result;
}

ProviderOperatorReviewEventValidationResult
validate_provider_operator_review_event(const ProviderOperatorReviewEvent &review) {
    ProviderOperatorReviewEventValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderOperatorReviewEventFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider operator review event format_version must be '" +
                std::string(kProviderOperatorReviewEventFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_telemetry_summary_format_version !=
        kProviderTelemetrySummaryFormatVersion) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider operator review event source format_version must be '" +
                std::string(kProviderTelemetrySummaryFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_telemetry_summary_identity.empty() ||
        review.operator_review_summary.empty() || review.next_step_recommendation.empty()) {
        provider_audit_detail_emit_validation_error(
            diagnostics,
            "provider operator review event identity and summary fields must not be empty");
    }
    return result;
}

ProviderExecutionAuditEventResult
build_provider_execution_audit_event(const ProviderFailureTaxonomyReport &report) {
    ProviderExecutionAuditEventResult result;
    result.diagnostics.append(validate_provider_failure_taxonomy_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderExecutionAuditEvent event;
    event.workflow_canonical_name = report.workflow_canonical_name;
    event.session_id = report.session_id;
    event.run_id = report.run_id;
    event.input_fixture = report.input_fixture;
    event.durable_store_import_provider_failure_taxonomy_report_identity =
        report.durable_store_import_provider_failure_taxonomy_report_identity;
    event.outcome = provider_audit_detail_outcome_for_failure(report.failure_kind);
    event.failure_kind = report.failure_kind;
    event.retryability = report.retryability;
    event.security_sensitivity = report.security_sensitivity;
    event.durable_store_import_provider_execution_audit_event_identity =
        "durable-store-import-provider-execution-audit-event::" + event.session_id +
        "::" + provider_audit_detail_outcome_slug(event.outcome);
    event.event_summary =
        "provider execution audit outcome is " + provider_audit_detail_outcome_slug(event.outcome);

    result.diagnostics.append(validate_provider_execution_audit_event(event).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.event = std::move(event);
    return result;
}

ProviderTelemetrySummaryResult
build_provider_telemetry_summary(const ProviderExecutionAuditEvent &event) {
    ProviderTelemetrySummaryResult result;
    result.diagnostics.append(validate_provider_execution_audit_event(event).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderTelemetrySummary summary;
    summary.workflow_canonical_name = event.workflow_canonical_name;
    summary.session_id = event.session_id;
    summary.run_id = event.run_id;
    summary.input_fixture = event.input_fixture;
    summary.durable_store_import_provider_execution_audit_event_identity =
        event.durable_store_import_provider_execution_audit_event_identity;
    summary.outcome = event.outcome;
    summary.provider_write_committed = event.outcome == ProviderAuditOutcome::Success;
    summary.retry_recommended = event.outcome == ProviderAuditOutcome::RetryPending;
    summary.recovery_required = event.outcome == ProviderAuditOutcome::RecoveryPending;
    summary.durable_store_import_provider_telemetry_summary_identity =
        "durable-store-import-provider-telemetry-summary::" + event.session_id +
        "::" + provider_audit_detail_outcome_slug(event.outcome);
    summary.telemetry_summary = "provider telemetry summary outcome is " +
                                provider_audit_detail_outcome_slug(event.outcome);

    result.diagnostics.append(validate_provider_telemetry_summary(summary).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.summary = std::move(summary);
    return result;
}

ProviderOperatorReviewEventResult
build_provider_operator_review_event(const ProviderTelemetrySummary &summary) {
    ProviderOperatorReviewEventResult result;
    result.diagnostics.append(validate_provider_telemetry_summary(summary).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderOperatorReviewEvent review;
    review.workflow_canonical_name = summary.workflow_canonical_name;
    review.session_id = summary.session_id;
    review.run_id = summary.run_id;
    review.input_fixture = summary.input_fixture;
    review.durable_store_import_provider_telemetry_summary_identity =
        summary.durable_store_import_provider_telemetry_summary_identity;
    review.outcome = summary.outcome;
    review.next_action = provider_audit_detail_next_action_for_outcome(summary.outcome);
    review.operator_review_summary = "provider operator review event outcome is " +
                                     provider_audit_detail_outcome_slug(summary.outcome);
    review.next_step_recommendation = review.next_action == ProviderAuditNextActionKind::Archive
                                          ? "archive audit event"
                                          : "operator action is required before archive";

    result.diagnostics.append(validate_provider_operator_review_event(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_compatibility.cpp ----

#include "ahfl/durable_store_import/provider_compatibility.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_compatibility_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_COMPATIBILITY";

void provider_compatibility_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                         std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_compatibility_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_compatibility_detail_status_slug(ProviderCompatibilityStatus status) {
    switch (status) {
    case ProviderCompatibilityStatus::Passed:
        return "passed";
    case ProviderCompatibilityStatus::Failed:
        return "failed";
    case ProviderCompatibilityStatus::Blocked:
        return "blocked";
    }
    return "blocked";
}

} // namespace

ProviderCompatibilityTestManifestValidationResult
validate_provider_compatibility_test_manifest(const ProviderCompatibilityTestManifest &manifest) {
    ProviderCompatibilityTestManifestValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (manifest.format_version != kProviderCompatibilityTestManifestFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility test manifest format_version must be '" +
                std::string(kProviderCompatibilityTestManifestFormatVersion) + "'");
    }
    if (manifest.source_durable_store_import_provider_operator_review_event_format_version !=
        kProviderOperatorReviewEventFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility test manifest source format_version must be '" +
                std::string(kProviderOperatorReviewEventFormatVersion) + "'");
    }
    if (manifest.workflow_canonical_name.empty() || manifest.session_id.empty() ||
        manifest.input_fixture.empty() ||
        manifest.durable_store_import_provider_operator_review_event_identity.empty() ||
        manifest.durable_store_import_provider_compatibility_test_manifest_identity.empty() ||
        manifest.capability_matrix_reference.empty()) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics, "provider compatibility test manifest identity fields must not be empty");
    }
    if (!manifest.includes_mock_adapter || !manifest.includes_local_filesystem_alpha ||
        manifest.provider_count < 2 || manifest.fixture_count < 4 ||
        manifest.external_service_required) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility test manifest must cover mock and alpha "
            "providers without external services");
    }
    return result;
}

ProviderFixtureMatrixValidationResult
validate_provider_fixture_matrix(const ProviderFixtureMatrix &matrix) {
    ProviderFixtureMatrixValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (matrix.format_version != kProviderFixtureMatrixFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider fixture matrix format_version must be '" +
                std::string(kProviderFixtureMatrixFormatVersion) + "'");
    }
    if (matrix.source_durable_store_import_provider_compatibility_test_manifest_format_version !=
        kProviderCompatibilityTestManifestFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider fixture matrix source format_version must be '" +
                std::string(kProviderCompatibilityTestManifestFormatVersion) + "'");
    }
    if (matrix.workflow_canonical_name.empty() || matrix.session_id.empty() ||
        matrix.input_fixture.empty() ||
        matrix.durable_store_import_provider_compatibility_test_manifest_identity.empty() ||
        matrix.durable_store_import_provider_fixture_matrix_identity.empty()) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics, "provider fixture matrix identity fields must not be empty");
    }
    if (!matrix.covers_mock_adapter || !matrix.covers_local_filesystem_alpha ||
        !matrix.covers_success_fixture || !matrix.covers_timeout_fixture ||
        !matrix.covers_conflict_fixture || !matrix.covers_schema_mismatch_fixture ||
        matrix.provider_count < 2 || matrix.fixture_count < 4 || matrix.external_service_required) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider fixture matrix must cover mock and alpha fixture paths "
            "without external services");
    }
    return result;
}

ProviderCompatibilityReportValidationResult
validate_provider_compatibility_report(const ProviderCompatibilityReport &report) {
    ProviderCompatibilityReportValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (report.format_version != kProviderCompatibilityReportFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility report format_version must be '" +
                std::string(kProviderCompatibilityReportFormatVersion) + "'");
    }
    if (report.source_durable_store_import_provider_fixture_matrix_format_version !=
            kProviderFixtureMatrixFormatVersion ||
        report.source_durable_store_import_provider_telemetry_summary_format_version !=
            kProviderTelemetrySummaryFormatVersion) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility report source format_versions must match "
            "V0.40 fixture matrix and V0.39 telemetry summary");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_fixture_matrix_identity.empty() ||
        report.durable_store_import_provider_telemetry_summary_identity.empty() ||
        report.durable_store_import_provider_compatibility_report_identity.empty() ||
        report.compatibility_summary.empty()) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility report identity and summary fields must not be empty");
    }
    if (report.external_service_required) {
        provider_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider compatibility report cannot require external services by default");
    }
    return result;
}

ProviderCompatibilityTestManifestResult
build_provider_compatibility_test_manifest(const ProviderOperatorReviewEvent &review) {
    ProviderCompatibilityTestManifestResult result;
    result.diagnostics.append(validate_provider_operator_review_event(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCompatibilityTestManifest manifest;
    manifest.workflow_canonical_name = review.workflow_canonical_name;
    manifest.session_id = review.session_id;
    manifest.run_id = review.run_id;
    manifest.input_fixture = review.input_fixture;
    manifest.durable_store_import_provider_operator_review_event_identity =
        review.durable_store_import_provider_telemetry_summary_identity;
    manifest.durable_store_import_provider_compatibility_test_manifest_identity =
        "durable-store-import-provider-compatibility-test-manifest::" + manifest.session_id;
    manifest.capability_matrix_reference =
        "provider-compatibility-matrix::mock-adapter::local-filesystem-alpha";

    result.diagnostics.append(validate_provider_compatibility_test_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.manifest = std::move(manifest);
    return result;
}

ProviderFixtureMatrixResult
build_provider_fixture_matrix(const ProviderCompatibilityTestManifest &manifest) {
    ProviderFixtureMatrixResult result;
    result.diagnostics.append(validate_provider_compatibility_test_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFixtureMatrix matrix;
    matrix.workflow_canonical_name = manifest.workflow_canonical_name;
    matrix.session_id = manifest.session_id;
    matrix.run_id = manifest.run_id;
    matrix.input_fixture = manifest.input_fixture;
    matrix.durable_store_import_provider_compatibility_test_manifest_identity =
        manifest.durable_store_import_provider_compatibility_test_manifest_identity;
    matrix.durable_store_import_provider_fixture_matrix_identity =
        "durable-store-import-provider-fixture-matrix::" + matrix.session_id;
    matrix.provider_count = manifest.provider_count;
    matrix.fixture_count = manifest.fixture_count;

    result.diagnostics.append(validate_provider_fixture_matrix(matrix).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.matrix = std::move(matrix);
    return result;
}

ProviderCompatibilityReportResult
build_provider_compatibility_report(const ProviderFixtureMatrix &matrix,
                                    const ProviderTelemetrySummary &telemetry) {
    ProviderCompatibilityReportResult result;
    result.diagnostics.append(validate_provider_fixture_matrix(matrix).diagnostics);
    result.diagnostics.append(validate_provider_telemetry_summary(telemetry).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCompatibilityReport report;
    report.workflow_canonical_name = matrix.workflow_canonical_name;
    report.session_id = matrix.session_id;
    report.run_id = matrix.run_id;
    report.input_fixture = matrix.input_fixture;
    report.durable_store_import_provider_fixture_matrix_identity =
        matrix.durable_store_import_provider_fixture_matrix_identity;
    report.durable_store_import_provider_telemetry_summary_identity =
        telemetry.durable_store_import_provider_telemetry_summary_identity;
    report.mock_adapter_compatible = matrix.covers_mock_adapter;
    report.local_filesystem_alpha_compatible = matrix.covers_local_filesystem_alpha;
    report.capability_matrix_complete =
        matrix.covers_success_fixture && matrix.covers_timeout_fixture &&
        matrix.covers_conflict_fixture && matrix.covers_schema_mismatch_fixture;
    if (telemetry.outcome == ProviderAuditOutcome::Blocked) {
        report.status = ProviderCompatibilityStatus::Blocked;
    } else if (report.mock_adapter_compatible && report.local_filesystem_alpha_compatible &&
               report.capability_matrix_complete) {
        report.status = ProviderCompatibilityStatus::Passed;
    } else {
        report.status = ProviderCompatibilityStatus::Failed;
    }
    report.durable_store_import_provider_compatibility_report_identity =
        "durable-store-import-provider-compatibility-report::" + report.session_id +
        "::" + provider_compatibility_detail_status_slug(report.status);
    report.compatibility_summary = "provider compatibility suite status is " +
                                   provider_compatibility_detail_status_slug(report.status);

    result.diagnostics.append(validate_provider_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_registry.cpp ----

#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_registry_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_REGISTRY";

void provider_registry_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                    std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_registry_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_registry_detail_selection_slug(ProviderSelectionStatus status) {
    switch (status) {
    case ProviderSelectionStatus::Selected:
        return "selected";
    case ProviderSelectionStatus::FallbackSelected:
        return "fallback-selected";
    case ProviderSelectionStatus::Blocked:
        return "blocked";
    }
    return "blocked";
}

[[nodiscard]] ProviderCapabilityNegotiationStatus
provider_registry_detail_negotiation_status_for_selection(ProviderSelectionStatus status) {
    switch (status) {
    case ProviderSelectionStatus::Selected:
        return ProviderCapabilityNegotiationStatus::Compatible;
    case ProviderSelectionStatus::FallbackSelected:
        return ProviderCapabilityNegotiationStatus::Degraded;
    case ProviderSelectionStatus::Blocked:
        return ProviderCapabilityNegotiationStatus::Blocked;
    }
    return ProviderCapabilityNegotiationStatus::Blocked;
}

} // namespace

ProviderRegistryValidationResult validate_provider_registry(const ProviderRegistry &registry) {
    ProviderRegistryValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (registry.format_version != kProviderRegistryFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider registry format_version must be '" +
                std::string(kProviderRegistryFormatVersion) + "'");
    }
    if (registry.source_durable_store_import_provider_compatibility_report_format_version !=
        kProviderCompatibilityReportFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider registry source format_version must be '" +
                std::string(kProviderCompatibilityReportFormatVersion) + "'");
    }
    if (registry.workflow_canonical_name.empty() || registry.session_id.empty() ||
        registry.input_fixture.empty() ||
        registry.durable_store_import_provider_compatibility_report_identity.empty() ||
        registry.durable_store_import_provider_registry_identity.empty() ||
        registry.primary_provider_id.empty() || registry.fallback_provider_id.empty()) {
        provider_registry_detail_emit_validation_error(
            diagnostics, "provider registry identity and provider fields must not be empty");
    }
    if (!registry.mock_adapter_registered || !registry.local_filesystem_alpha_registered ||
        registry.unaudited_fallback_allowed || registry.registered_provider_count < 2) {
        provider_registry_detail_emit_validation_error(
            diagnostics, "provider registry must register audited mock and alpha providers only");
    }
    return result;
}

ProviderSelectionPlanValidationResult
validate_provider_selection_plan(const ProviderSelectionPlan &plan) {
    ProviderSelectionPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderSelectionPlanFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider selection plan format_version must be '" +
                std::string(kProviderSelectionPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_registry_format_version !=
        kProviderRegistryFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider selection plan source format_version must be '" +
                std::string(kProviderRegistryFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_registry_identity.empty() ||
        plan.durable_store_import_provider_selection_plan_identity.empty() ||
        plan.selected_provider_id.empty() || plan.fallback_provider_id.empty() ||
        plan.fallback_policy.empty() || plan.capability_gap_summary.empty()) {
        provider_registry_detail_emit_validation_error(
            diagnostics, "provider selection plan identity and policy fields must not be empty");
    }
    if (plan.selection_status == ProviderSelectionStatus::Blocked &&
        !plan.requires_operator_review) {
        provider_registry_detail_emit_validation_error(
            diagnostics, "blocked provider selection plan must require operator review");
    }
    return result;
}

ProviderCapabilityNegotiationReviewValidationResult
validate_provider_capability_negotiation_review(const ProviderCapabilityNegotiationReview &review) {
    ProviderCapabilityNegotiationReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderCapabilityNegotiationReviewFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider capability negotiation review format_version must be '" +
                std::string(kProviderCapabilityNegotiationReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_selection_plan_format_version !=
        kProviderSelectionPlanFormatVersion) {
        provider_registry_detail_emit_validation_error(
            diagnostics,
            "provider capability negotiation review source format_version must be '" +
                std::string(kProviderSelectionPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_selection_plan_identity.empty() ||
        review.selected_provider_id.empty() || review.negotiation_summary.empty() ||
        review.operator_recommendation.empty()) {
        provider_registry_detail_emit_validation_error(
            diagnostics, "provider capability negotiation review fields must not be empty");
    }
    return result;
}

ProviderRegistryResult build_provider_registry(const ProviderCompatibilityReport &report) {
    ProviderRegistryResult result;
    result.diagnostics.append(validate_provider_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderRegistry registry;
    registry.workflow_canonical_name = report.workflow_canonical_name;
    registry.session_id = report.session_id;
    registry.run_id = report.run_id;
    registry.input_fixture = report.input_fixture;
    registry.durable_store_import_provider_compatibility_report_identity =
        report.durable_store_import_provider_compatibility_report_identity;
    registry.durable_store_import_provider_registry_identity =
        "durable-store-import-provider-registry::" + registry.session_id;
    registry.compatibility_status = report.status;

    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.registry = std::move(registry);
    return result;
}

ProviderSelectionPlanResult build_provider_selection_plan(const ProviderRegistry &registry) {
    ProviderSelectionPlanResult result;
    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderSelectionPlan plan;
    plan.workflow_canonical_name = registry.workflow_canonical_name;
    plan.session_id = registry.session_id;
    plan.run_id = registry.run_id;
    plan.input_fixture = registry.input_fixture;
    plan.durable_store_import_provider_registry_identity =
        registry.durable_store_import_provider_registry_identity;
    plan.fallback_provider_id = registry.fallback_provider_id;
    if (registry.compatibility_status == ProviderCompatibilityStatus::Passed) {
        plan.selection_status = ProviderSelectionStatus::Selected;
        plan.selected_provider_id = registry.primary_provider_id;
        plan.requires_operator_review = false;
        plan.capability_gap_summary = "no provider capability gap detected";
    } else if (registry.mock_adapter_registered &&
               registry.compatibility_status == ProviderCompatibilityStatus::Failed) {
        plan.selection_status = ProviderSelectionStatus::FallbackSelected;
        plan.selected_provider_id = registry.fallback_provider_id;
        plan.degradation_allowed = true;
        plan.capability_gap_summary =
            "primary provider compatibility failed; audited fallback selected";
    } else {
        plan.selection_status = ProviderSelectionStatus::Blocked;
        plan.selected_provider_id = registry.fallback_provider_id;
        plan.capability_gap_summary =
            "provider selection blocked until compatibility report passes";
    }
    plan.fallback_policy = "audited-fallback-only";
    plan.durable_store_import_provider_selection_plan_identity =
        "durable-store-import-provider-selection-plan::" + plan.session_id +
        "::" + provider_registry_detail_selection_slug(plan.selection_status);

    result.diagnostics.append(validate_provider_selection_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderCapabilityNegotiationReviewResult
build_provider_capability_negotiation_review(const ProviderSelectionPlan &plan) {
    ProviderCapabilityNegotiationReviewResult result;
    result.diagnostics.append(validate_provider_selection_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCapabilityNegotiationReview review;
    review.workflow_canonical_name = plan.workflow_canonical_name;
    review.session_id = plan.session_id;
    review.run_id = plan.run_id;
    review.input_fixture = plan.input_fixture;
    review.durable_store_import_provider_selection_plan_identity =
        plan.durable_store_import_provider_selection_plan_identity;
    review.selection_status = plan.selection_status;
    review.negotiation_status =
        provider_registry_detail_negotiation_status_for_selection(plan.selection_status);
    review.selected_provider_id = plan.selected_provider_id;
    review.negotiation_summary = "provider capability negotiation is " +
                                 provider_registry_detail_selection_slug(plan.selection_status);
    review.operator_recommendation = plan.requires_operator_review
                                         ? "operator review required before provider execution"
                                         : "provider selection can proceed";

    result.diagnostics.append(validate_provider_capability_negotiation_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_conformance.cpp ----

#include "ahfl/durable_store_import/provider_conformance.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_conformance_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_CONFORMANCE";

void provider_conformance_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                       std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_conformance_detail_kValidationDiagnosticCode, message);
}

// 执行单个检查并返回检查项
[[nodiscard]] ConformanceCheckItem
provider_conformance_detail_run_check(const std::string &check_name,
                                      bool condition,
                                      const std::string &artifact_reference,
                                      const std::string &failure_reason_if_fail) {
    ConformanceCheckItem item;
    item.check_name = check_name;
    item.artifact_reference = artifact_reference;
    if (condition) {
        item.result = ConformanceCheckResult::Pass;
    } else {
        item.result = ConformanceCheckResult::Fail;
        item.failure_reason = failure_reason_if_fail;
    }
    return item;
}

} // namespace

ProviderConformanceReportValidationResult
validate_provider_conformance_report(const ProviderConformanceReport &report) {
    ProviderConformanceReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    // 检查 format_version
    if (report.format_version != kProviderConformanceReportFormatVersion) {
        provider_conformance_detail_emit_validation_error(
            diagnostics,
            "provider conformance report format_version must be '" +
                std::string(kProviderConformanceReportFormatVersion) + "'");
    }

    // 检查 source format versions
    if (report.source_durable_store_import_provider_compatibility_report_format_version !=
            kProviderCompatibilityReportFormatVersion ||
        report.source_durable_store_import_provider_registry_format_version !=
            kProviderRegistryFormatVersion ||
        report.source_durable_store_import_provider_production_readiness_evidence_format_version !=
            kProviderProductionReadinessEvidenceFormatVersion) {
        provider_conformance_detail_emit_validation_error(
            diagnostics,
            "provider conformance report source format_versions must match V0.40-V0.42 artifacts");
    }

    // 检查必需字段非空
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_compatibility_report_identity.empty() ||
        report.durable_store_import_provider_registry_identity.empty() ||
        report.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        report.durable_store_import_provider_conformance_report_identity.empty() ||
        report.conformance_summary.empty()) {
        provider_conformance_detail_emit_validation_error(
            diagnostics,
            "provider conformance report identity and summary fields must not be empty");
    }

    // 检查计数非负
    if (report.pass_count < 0 || report.fail_count < 0 || report.skipped_count < 0) {
        provider_conformance_detail_emit_validation_error(
            diagnostics, "provider conformance report counts cannot be negative");
    }

    return result;
}

ProviderConformanceReportResult
build_provider_conformance_report(const ProviderCompatibilityReport &compatibility_report,
                                  const ProviderRegistry &registry,
                                  const ProviderProductionReadinessEvidence &readiness_evidence) {
    ProviderConformanceReportResult result;

    // 验证上游 artifact
    result.diagnostics.append(
        validate_provider_compatibility_report(compatibility_report).diagnostics);
    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    result.diagnostics.append(
        validate_provider_production_readiness_evidence(readiness_evidence).diagnostics);

    if (result.has_errors()) {
        return result;
    }

    ProviderConformanceReport report;
    report.workflow_canonical_name = compatibility_report.workflow_canonical_name;
    report.session_id = compatibility_report.session_id;
    report.run_id = compatibility_report.run_id;
    report.input_fixture = compatibility_report.input_fixture;

    // 设置上游 artifact 引用
    report.durable_store_import_provider_compatibility_report_identity =
        compatibility_report.durable_store_import_provider_compatibility_report_identity;
    report.durable_store_import_provider_registry_identity =
        registry.durable_store_import_provider_registry_identity;
    report.durable_store_import_provider_production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // 预先生成 conformance report identity（后续检查需引用）
    report.durable_store_import_provider_conformance_report_identity =
        "durable-store-import-provider-conformance-report::" + report.session_id;

    // 执行契约检查
    std::vector<ConformanceCheckItem> checks;

    // 检查 1: Compatibility Status Pass
    checks.push_back(provider_conformance_detail_run_check(
        "compatibility_status_passed",
        compatibility_report.status == ProviderCompatibilityStatus::Passed,
        report.durable_store_import_provider_compatibility_report_identity,
        "compatibility status is not passed"));

    // 检查 2: Mock Adapter Compatible
    checks.push_back(provider_conformance_detail_run_check(
        "mock_adapter_compatible",
        compatibility_report.mock_adapter_compatible,
        report.durable_store_import_provider_compatibility_report_identity,
        "mock adapter is not compatible"));

    // 检查 3: Local Filesystem Alpha Compatible
    checks.push_back(provider_conformance_detail_run_check(
        "local_filesystem_alpha_compatible",
        compatibility_report.local_filesystem_alpha_compatible,
        report.durable_store_import_provider_compatibility_report_identity,
        "local filesystem alpha is not compatible"));

    // 检查 4: Capability Matrix Complete
    checks.push_back(provider_conformance_detail_run_check(
        "capability_matrix_complete",
        compatibility_report.capability_matrix_complete,
        report.durable_store_import_provider_compatibility_report_identity,
        "capability matrix is not complete"));

    // 检查 5: Registry Mock Adapter Registered
    checks.push_back(provider_conformance_detail_run_check(
        "registry_mock_adapter_registered",
        registry.mock_adapter_registered,
        report.durable_store_import_provider_registry_identity,
        "mock adapter is not registered in registry"));

    // 检查 6: Registry Local Filesystem Alpha Registered
    checks.push_back(provider_conformance_detail_run_check(
        "registry_local_filesystem_alpha_registered",
        registry.local_filesystem_alpha_registered,
        report.durable_store_import_provider_registry_identity,
        "local filesystem alpha is not registered in registry"));

    // 检查 7: Registry Compatibility Status Match
    checks.push_back(provider_conformance_detail_run_check(
        "registry_compatibility_status_match",
        registry.compatibility_status == ProviderCompatibilityStatus::Passed ||
            registry.compatibility_status == ProviderCompatibilityStatus::Failed,
        report.durable_store_import_provider_registry_identity,
        "registry compatibility status does not match expected values"));

    // 检查 8: Security Evidence Passed
    checks.push_back(provider_conformance_detail_run_check(
        "security_evidence_passed",
        readiness_evidence.security_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "security evidence did not pass"));

    // 检查 9: Recovery Evidence Passed
    checks.push_back(provider_conformance_detail_run_check(
        "recovery_evidence_passed",
        readiness_evidence.recovery_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "recovery evidence did not pass"));

    // 检查 10: Compatibility Evidence Passed
    checks.push_back(provider_conformance_detail_run_check(
        "compatibility_evidence_passed",
        readiness_evidence.compatibility_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "compatibility evidence did not pass"));

    // 检查 11: Observability Evidence Passed
    checks.push_back(provider_conformance_detail_run_check(
        "observability_evidence_passed",
        readiness_evidence.observability_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "observability evidence did not pass"));

    // 检查 12: Registry Evidence Passed
    checks.push_back(provider_conformance_detail_run_check(
        "registry_evidence_passed",
        readiness_evidence.registry_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "registry evidence did not pass"));

    // 检查 13: No Blocking Issues
    checks.push_back(provider_conformance_detail_run_check(
        "no_blocking_issues",
        readiness_evidence.blocking_issue_count == 0,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "there are " + std::to_string(readiness_evidence.blocking_issue_count) +
            " blocking issues"));

    // 检查 14: Session ID Consistency
    checks.push_back(provider_conformance_detail_run_check(
        "session_id_consistency",
        compatibility_report.session_id == registry.session_id &&
            registry.session_id == readiness_evidence.session_id,
        report.durable_store_import_provider_conformance_report_identity,
        "session IDs are not consistent across artifacts"));

    // 检查 15: Workflow Name Consistency
    checks.push_back(provider_conformance_detail_run_check(
        "workflow_name_consistency",
        compatibility_report.workflow_canonical_name == registry.workflow_canonical_name &&
            registry.workflow_canonical_name == readiness_evidence.workflow_canonical_name,
        report.durable_store_import_provider_conformance_report_identity,
        "workflow canonical names are not consistent across artifacts"));

    // 检查 16: Input Fixture Consistency
    checks.push_back(provider_conformance_detail_run_check(
        "input_fixture_consistency",
        compatibility_report.input_fixture == registry.input_fixture &&
            registry.input_fixture == readiness_evidence.input_fixture,
        report.durable_store_import_provider_conformance_report_identity,
        "input fixtures are not consistent across artifacts"));

    // 计算统计数据
    int pass_count = 0;
    int fail_count = 0;
    int skipped_count = 0;
    for (const auto &check : checks) {
        switch (check.result) {
        case ConformanceCheckResult::Pass:
            ++pass_count;
            break;
        case ConformanceCheckResult::Fail:
            ++fail_count;
            break;
        case ConformanceCheckResult::Skipped:
            ++skipped_count;
            break;
        }
    }

    report.pass_count = pass_count;
    report.fail_count = fail_count;
    report.skipped_count = skipped_count;
    report.checks = std::move(checks);

    // 生成 conformance summary
    if (fail_count == 0) {
        report.conformance_summary = "provider conformance passed all checks";
    } else {
        report.conformance_summary = "provider conformance failed " + std::to_string(fail_count) +
                                     " of " + std::to_string(checks.size()) + " checks";
    }

    // 验证生成的 report
    result.diagnostics.append(validate_provider_conformance_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_schema_compatibility.cpp ----

#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_schema_compatibility_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_SCHEMA_COMPATIBILITY";

void provider_schema_compatibility_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_schema_compatibility_detail_kValidationDiagnosticCode, message);
}

} // namespace

ProviderSchemaCompatibilityReportValidationResult
validate_provider_schema_compatibility_report(const ProviderSchemaCompatibilityReport &report) {
    ProviderSchemaCompatibilityReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    // 校验 format version
    if (report.format_version != kProviderSchemaCompatibilityReportFormatVersion) {
        provider_schema_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider schema compatibility report format_version must be '" +
                std::string(kProviderSchemaCompatibilityReportFormatVersion) + "'");
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty() || report.session_id.empty()) {
        provider_schema_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider schema compatibility report identity fields must not "
            "be empty");
    }

    // 校验兼容性汇总不为空
    if (report.compatibility_summary.empty()) {
        provider_schema_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider schema compatibility report compatibility_summary must not be empty");
    }

    // 校验汇总计数与检查项一致
    const int total = report.compatible_count + report.incompatible_count + report.unknown_count;
    const int expected_total = static_cast<int>(report.version_checks.size());
    if (total != expected_total) {
        provider_schema_compatibility_detail_emit_validation_error(
            diagnostics,
            "provider schema compatibility report summary counts (" + std::to_string(total) +
                ") do not match version_checks count (" + std::to_string(expected_total) + ")");
    }

    return result;
}

ProviderSchemaCompatibilityReportResult build_provider_schema_compatibility_report(
    const std::vector<ArtifactVersionCheck> &version_checks,
    const std::vector<SourceChainCheck> &source_chain_checks,
    const std::vector<ReferenceVersionCheck> &reference_checks,
    const std::string &workflow_canonical_name,
    const std::string &session_id,
    const std::optional<std::string> &run_id) {
    ProviderSchemaCompatibilityReportResult result;

    // 校验输入不为空
    if (workflow_canonical_name.empty() || session_id.empty()) {
        provider_schema_compatibility_detail_emit_validation_error(
            result.diagnostics, "workflow_canonical_name and session_id must not be empty");
        return result;
    }

    ProviderSchemaCompatibilityReport report;
    report.workflow_canonical_name = workflow_canonical_name;
    report.session_id = session_id;
    report.run_id = run_id;
    report.version_checks = version_checks;
    report.source_chain_checks = source_chain_checks;
    report.reference_checks = reference_checks;

    // 计算汇总
    int compatible = 0;
    int incompatible = 0;
    int unknown = 0;
    for (const auto &check : version_checks) {
        switch (check.status) {
        case SchemaCompatibilityStatus::Compatible:
            ++compatible;
            break;
        case SchemaCompatibilityStatus::Incompatible:
            ++incompatible;
            break;
        case SchemaCompatibilityStatus::Unknown:
            ++unknown;
            break;
        }
    }
    report.compatible_count = compatible;
    report.incompatible_count = incompatible;
    report.unknown_count = unknown;

    // 检查 source chain 是否有不兼容项
    bool source_chain_drift = false;
    for (const auto &chain : source_chain_checks) {
        if (!chain.is_compatible) {
            source_chain_drift = true;
            break;
        }
    }

    // 检查 reference version 是否有不兼容项
    bool reference_drift = false;
    for (const auto &ref : reference_checks) {
        if (!ref.is_compatible) {
            reference_drift = true;
            break;
        }
    }

    // 判定 schema drift
    report.has_schema_drift = (incompatible > 0) || source_chain_drift || reference_drift;
    if (report.has_schema_drift) {
        report.drift_details =
            "schema drift detected: " + std::to_string(incompatible) + " incompatible version(s)";
        if (source_chain_drift) {
            report.drift_details.value() += ", source chain incompatibility";
        }
        if (reference_drift) {
            report.drift_details.value() += ", reference version incompatibility";
        }
    }

    // 构造汇总
    report.compatibility_summary = "provider schema compatibility: " + std::to_string(compatible) +
                                   " compatible, " + std::to_string(incompatible) +
                                   " incompatible, " + std::to_string(unknown) + " unknown";

    // 验证构建结果
    result.diagnostics.append(validate_provider_schema_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import

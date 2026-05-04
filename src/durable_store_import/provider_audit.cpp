#include "ahfl/durable_store_import/provider_audit.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_AUDIT";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string outcome_slug(ProviderAuditOutcome outcome) {
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

[[nodiscard]] ProviderAuditOutcome outcome_for_failure(ProviderFailureKindV1 kind) {
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

[[nodiscard]] ProviderAuditNextActionKind next_action_for_outcome(ProviderAuditOutcome outcome) {
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
        emit_validation_error(diagnostics,
                              "provider execution audit event format_version must be '" +
                                  std::string(kProviderExecutionAuditEventFormatVersion) + "'");
    }
    if (event.source_durable_store_import_provider_failure_taxonomy_report_format_version !=
        kProviderFailureTaxonomyReportFormatVersion) {
        emit_validation_error(diagnostics,
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
        emit_validation_error(
            diagnostics,
            "provider execution audit event identity and summary fields must not be empty");
    }
    if (!event.secret_free || event.raw_telemetry_persisted) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "provider telemetry summary format_version must be '" +
                                  std::string(kProviderTelemetrySummaryFormatVersion) + "'");
    }
    if (summary.source_durable_store_import_provider_execution_audit_event_format_version !=
        kProviderExecutionAuditEventFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider telemetry summary source format_version must be '" +
                                  std::string(kProviderExecutionAuditEventFormatVersion) + "'");
    }
    if (summary.workflow_canonical_name.empty() || summary.session_id.empty() ||
        summary.input_fixture.empty() ||
        summary.durable_store_import_provider_execution_audit_event_identity.empty() ||
        summary.durable_store_import_provider_telemetry_summary_identity.empty() ||
        summary.telemetry_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider telemetry summary identity and summary fields must not be empty");
    }
    if (!summary.secret_free || summary.raw_telemetry_persisted) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "provider operator review event format_version must be '" +
                                  std::string(kProviderOperatorReviewEventFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_telemetry_summary_format_version !=
        kProviderTelemetrySummaryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider operator review event source format_version must be '" +
                                  std::string(kProviderTelemetrySummaryFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_telemetry_summary_identity.empty() ||
        review.operator_review_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(
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
    event.outcome = outcome_for_failure(report.failure_kind);
    event.failure_kind = report.failure_kind;
    event.retryability = report.retryability;
    event.security_sensitivity = report.security_sensitivity;
    event.durable_store_import_provider_execution_audit_event_identity =
        "durable-store-import-provider-execution-audit-event::" + event.session_id +
        "::" + outcome_slug(event.outcome);
    event.event_summary = "provider execution audit outcome is " + outcome_slug(event.outcome);

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
        "::" + outcome_slug(event.outcome);
    summary.telemetry_summary =
        "provider telemetry summary outcome is " + outcome_slug(event.outcome);

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
    review.next_action = next_action_for_outcome(summary.outcome);
    review.operator_review_summary =
        "provider operator review event outcome is " + outcome_slug(summary.outcome);
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

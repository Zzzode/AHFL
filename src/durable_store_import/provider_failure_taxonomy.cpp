#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_FAILURE_TAXONOMY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string failure_kind_slug(ProviderFailureKindV1 kind) {
    switch (kind) {
    case ProviderFailureKindV1::None:
        return "none";
    case ProviderFailureKindV1::Authentication:
        return "authentication";
    case ProviderFailureKindV1::Authorization:
        return "authorization";
    case ProviderFailureKindV1::Network:
        return "network";
    case ProviderFailureKindV1::Throttling:
        return "throttling";
    case ProviderFailureKindV1::Conflict:
        return "conflict";
    case ProviderFailureKindV1::SchemaMismatch:
        return "schema-mismatch";
    case ProviderFailureKindV1::ProviderInternal:
        return "provider-internal";
    case ProviderFailureKindV1::Unknown:
        return "unknown";
    case ProviderFailureKindV1::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] std::string action_summary(ProviderFailureOperatorActionV1 action) {
    switch (action) {
    case ProviderFailureOperatorActionV1::None:
        return "no operator action required";
    case ProviderFailureOperatorActionV1::RotateCredentials:
        return "rotate credential handle before retry";
    case ProviderFailureOperatorActionV1::GrantPermission:
        return "grant provider permission before retry";
    case ProviderFailureOperatorActionV1::RetryLater:
        return "retry later with idempotency token";
    case ProviderFailureOperatorActionV1::InspectDuplicate:
        return "inspect provider commit for duplicate write";
    case ProviderFailureOperatorActionV1::FixSchema:
        return "fix payload schema before retry";
    case ProviderFailureOperatorActionV1::EscalateProvider:
        return "escalate provider internal error";
    case ProviderFailureOperatorActionV1::ManualReview:
        return "manual review required";
    }
    return "manual review required";
}

void apply_mapping(ProviderSdkMockAdapterNormalizedResultKind result_kind,
                   ProviderFailureTaxonomyReport &report) {
    switch (result_kind) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        report.failure_kind = ProviderFailureKindV1::None;
        report.failure_category = ProviderFailureCategoryV1::None;
        report.retryability = ProviderFailureRetryabilityV1::NotApplicable;
        report.operator_action = ProviderFailureOperatorActionV1::None;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::None;
        report.redacted_failure_summary = "accepted provider result has no failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
        report.failure_kind = ProviderFailureKindV1::Network;
        report.failure_category = ProviderFailureCategoryV1::Transport;
        report.retryability = ProviderFailureRetryabilityV1::Retryable;
        report.operator_action = ProviderFailureOperatorActionV1::RetryLater;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider timeout mapped to retryable network failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        report.failure_kind = ProviderFailureKindV1::Throttling;
        report.failure_category = ProviderFailureCategoryV1::RateLimit;
        report.retryability = ProviderFailureRetryabilityV1::Retryable;
        report.operator_action = ProviderFailureOperatorActionV1::RetryLater;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary =
            "provider throttle mapped to retryable rate limit failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        report.failure_kind = ProviderFailureKindV1::Conflict;
        report.failure_category = ProviderFailureCategoryV1::Consistency;
        report.retryability = ProviderFailureRetryabilityV1::DuplicateReviewRequired;
        report.operator_action = ProviderFailureOperatorActionV1::InspectDuplicate;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider conflict mapped to duplicate review";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        report.failure_kind = ProviderFailureKindV1::SchemaMismatch;
        report.failure_category = ProviderFailureCategoryV1::Contract;
        report.retryability = ProviderFailureRetryabilityV1::NonRetryable;
        report.operator_action = ProviderFailureOperatorActionV1::FixSchema;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider schema mismatch requires payload fix";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
        report.failure_kind = ProviderFailureKindV1::ProviderInternal;
        report.failure_category = ProviderFailureCategoryV1::Provider;
        report.retryability = ProviderFailureRetryabilityV1::NonRetryable;
        report.operator_action = ProviderFailureOperatorActionV1::EscalateProvider;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::Unknown;
        report.redacted_failure_summary = "provider internal failure requires escalation";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        report.failure_kind = ProviderFailureKindV1::Blocked;
        report.failure_category = ProviderFailureCategoryV1::Blocked;
        report.retryability = ProviderFailureRetryabilityV1::Blocked;
        report.operator_action = ProviderFailureOperatorActionV1::ManualReview;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::Unknown;
        report.redacted_failure_summary = "provider result blocked before failure taxonomy mapping";
        return;
    }
}

} // namespace

ProviderFailureTaxonomyReportValidationResult
validate_provider_failure_taxonomy_report(const ProviderFailureTaxonomyReport &report) {
    ProviderFailureTaxonomyReportValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (report.format_version != kProviderFailureTaxonomyReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy report format_version must be '" +
                                  std::string(kProviderFailureTaxonomyReportFormatVersion) + "'");
    }
    if (report
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy report adapter source format_version must "
                              "be '" +
                                  std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) +
                                  "'");
    }
    if (report.source_durable_store_import_provider_write_recovery_plan_format_version !=
        kProviderWriteRecoveryPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy report recovery source format_version "
                              "must be '" +
                                  std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        report.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        report.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        report.redacted_failure_summary.empty() || report.recovery_review_failure_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider failure taxonomy report identity and summary fields must not be empty");
    }
    if (report.secret_bearing_error_persisted || report.raw_provider_error_persisted) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy report must not persist secret-bearing or "
                              "raw provider errors");
    }
    return result;
}

ProviderFailureTaxonomyReviewValidationResult
validate_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReview &review) {
    ProviderFailureTaxonomyReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderFailureTaxonomyReviewFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy review format_version must be '" +
                                  std::string(kProviderFailureTaxonomyReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_failure_taxonomy_report_format_version !=
        kProviderFailureTaxonomyReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider failure taxonomy review source format_version must be '" +
                                  std::string(kProviderFailureTaxonomyReportFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        review.taxonomy_review_summary.empty() || review.operator_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider failure taxonomy review identity and summary fields must not be empty");
    }
    return result;
}

ProviderFailureTaxonomyReportResult
build_provider_failure_taxonomy_report(const ProviderSdkMockAdapterExecutionResult &adapter_result,
                                       const ProviderWriteRecoveryPlan &recovery_plan) {
    ProviderFailureTaxonomyReportResult result;
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(adapter_result).diagnostics);
    result.diagnostics.append(validate_provider_write_recovery_plan(recovery_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFailureTaxonomyReport report;
    report.workflow_canonical_name = adapter_result.workflow_canonical_name;
    report.session_id = adapter_result.session_id;
    report.run_id = adapter_result.run_id;
    report.input_fixture = adapter_result.input_fixture;
    report.durable_store_import_provider_sdk_mock_adapter_result_identity =
        adapter_result.durable_store_import_provider_sdk_mock_adapter_result_identity;
    report.durable_store_import_provider_write_recovery_plan_identity =
        recovery_plan.durable_store_import_provider_write_recovery_plan_identity;
    report.source_normalized_result = adapter_result.normalized_result;
    apply_mapping(adapter_result.normalized_result, report);
    report.recovery_review_failure_summary = recovery_plan.partial_write_recovery_plan;
    report.durable_store_import_provider_failure_taxonomy_report_identity =
        "durable-store-import-provider-failure-taxonomy-report::" + report.session_id +
        "::" + failure_kind_slug(report.failure_kind);

    result.diagnostics.append(validate_provider_failure_taxonomy_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

ProviderFailureTaxonomyReviewResult
build_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReport &report) {
    ProviderFailureTaxonomyReviewResult result;
    result.diagnostics.append(validate_provider_failure_taxonomy_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFailureTaxonomyReview review;
    review.workflow_canonical_name = report.workflow_canonical_name;
    review.session_id = report.session_id;
    review.run_id = report.run_id;
    review.input_fixture = report.input_fixture;
    review.durable_store_import_provider_failure_taxonomy_report_identity =
        report.durable_store_import_provider_failure_taxonomy_report_identity;
    review.failure_kind = report.failure_kind;
    review.retryability = report.retryability;
    review.operator_action = report.operator_action;
    review.security_sensitivity = report.security_sensitivity;
    review.taxonomy_review_summary =
        "provider failure taxonomy kind is " + failure_kind_slug(report.failure_kind);
    review.operator_recommendation = action_summary(report.operator_action);

    result.diagnostics.append(validate_provider_failure_taxonomy_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

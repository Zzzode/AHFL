#include "ahfl/durable_store_import/provider_retry.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_WRITE_RETRY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string normalized_slug(ProviderSdkMockAdapterNormalizedResultKind result) {
    switch (result) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        return "accepted";
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
        return "provider-failure";
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
        return "timeout";
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        return "throttled";
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        return "conflict";
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        return "schema-mismatch";
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] ProviderWriteRetryFailureAttribution
failure_for_result(const ProviderSdkMockAdapterExecutionResult &adapter_result,
                   ProviderWriteRetryFailureKind kind,
                   std::string fallback) {
    if (adapter_result.failure_attribution.has_value()) {
        fallback = adapter_result.failure_attribution->message;
    }
    return ProviderWriteRetryFailureAttribution{
        .kind = kind,
        .message = std::move(fallback),
    };
}

void validate_failure(const std::optional<ProviderWriteRetryFailureAttribution> &failure,
                      DiagnosticBag &diagnostics) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics,
            "provider write retry decision failure_attribution message must not be empty");
    }
}

} // namespace

ProviderWriteRetryDecisionValidationResult
validate_provider_write_retry_decision(const ProviderWriteRetryDecision &decision) {
    ProviderWriteRetryDecisionValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (decision.format_version != kProviderWriteRetryDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write retry decision format_version must be '" +
                                  std::string(kProviderWriteRetryDecisionFormatVersion) + "'");
    }
    if (decision
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write retry decision source format_version must be '" +
                                  std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) +
                                  "'");
    }
    if (decision.workflow_canonical_name.empty() || decision.session_id.empty() ||
        decision.input_fixture.empty() ||
        decision.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        decision.durable_store_import_provider_write_retry_decision_identity.empty() ||
        decision.idempotency_key_namespace.empty() || decision.idempotency_key.empty() ||
        decision.retry_token_version != kProviderWriteRetryTokenVersion ||
        decision.retry_token_placeholder_identity.empty() ||
        decision.duplicate_detection_summary.empty() || decision.retry_decision_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider write retry decision identity and summary fields must not be empty");
    }
    validate_failure(decision.failure_attribution, diagnostics);
    if (decision.retry_allowed &&
        decision.retry_eligibility != ProviderWriteRetryEligibility::Retryable) {
        emit_validation_error(
            diagnostics,
            "provider write retry decision retry_allowed requires retryable eligibility");
    }
    if (decision.duplicate_write_possible &&
        decision.retry_eligibility != ProviderWriteRetryEligibility::DuplicateReviewRequired) {
        emit_validation_error(
            diagnostics,
            "provider write retry decision duplicate flag requires duplicate review eligibility");
    }
    if (decision.retry_eligibility == ProviderWriteRetryEligibility::Blocked &&
        !decision.failure_attribution.has_value()) {
        emit_validation_error(
            diagnostics,
            "provider write retry decision blocked status requires failure_attribution");
    }
    return result;
}

ProviderWriteRetryDecisionResult
build_provider_write_retry_decision(const ProviderSdkMockAdapterExecutionResult &adapter_result) {
    ProviderWriteRetryDecisionResult result;
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(adapter_result).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderWriteRetryDecision decision;
    decision.workflow_canonical_name = adapter_result.workflow_canonical_name;
    decision.session_id = adapter_result.session_id;
    decision.run_id = adapter_result.run_id;
    decision.input_fixture = adapter_result.input_fixture;
    decision.durable_store_import_provider_sdk_mock_adapter_result_identity =
        adapter_result.durable_store_import_provider_sdk_mock_adapter_result_identity;
    decision.source_normalized_result = adapter_result.normalized_result;
    decision.durable_store_import_provider_write_retry_decision_identity =
        "durable-store-import-provider-write-retry-decision::" + adapter_result.session_id +
        "::" + normalized_slug(adapter_result.normalized_result);
    decision.idempotency_key = decision.idempotency_key_namespace +
                               "::" + adapter_result.session_id +
                               "::" + adapter_result.input_fixture;
    decision.retry_token_placeholder_identity =
        "provider-write-retry-token-placeholder::" + decision.idempotency_key;
    switch (adapter_result.normalized_result) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        decision.retry_eligibility = ProviderWriteRetryEligibility::NotApplicable;
        decision.duplicate_detection_summary = "accepted provider result has no retry requirement";
        decision.retry_decision_summary = "do not retry an accepted provider write";
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        decision.retry_eligibility = ProviderWriteRetryEligibility::Retryable;
        decision.retry_allowed = true;
        decision.duplicate_detection_summary =
            "retry must reuse idempotency key before provider write is attempted again";
        decision.retry_decision_summary = "retry is allowed with the retry token placeholder";
        decision.failure_attribution =
            failure_for_result(adapter_result,
                               ProviderWriteRetryFailureKind::RetryableProviderFailure,
                               "provider failure is retryable");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        decision.retry_eligibility = ProviderWriteRetryEligibility::DuplicateReviewRequired;
        decision.duplicate_write_possible = true;
        decision.duplicate_detection_summary =
            "conflict result may indicate duplicate write and requires provider commit lookup";
        decision.retry_decision_summary =
            "do not retry until duplicate write review resolves the conflict";
        decision.failure_attribution =
            failure_for_result(adapter_result,
                               ProviderWriteRetryFailureKind::DuplicateWriteSuspected,
                               "provider conflict may indicate duplicate write");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        decision.retry_eligibility = ProviderWriteRetryEligibility::NonRetryable;
        decision.duplicate_detection_summary = "no duplicate write signal was observed";
        decision.retry_decision_summary =
            "provider failure is not retryable without manual remediation";
        decision.failure_attribution =
            failure_for_result(adapter_result,
                               ProviderWriteRetryFailureKind::NonRetryableProviderFailure,
                               "provider failure is not retryable");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        decision.retry_eligibility = ProviderWriteRetryEligibility::Blocked;
        decision.duplicate_detection_summary =
            "adapter result was blocked before retry classification";
        decision.retry_decision_summary = "wait for adapter result before retry classification";
        decision.failure_attribution =
            failure_for_result(adapter_result,
                               ProviderWriteRetryFailureKind::AdapterResultNotReady,
                               "adapter result is not ready");
        break;
    }
    result.diagnostics.append(validate_provider_write_retry_decision(decision).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.decision = std::move(decision);
    return result;
}

} // namespace ahfl::durable_store_import

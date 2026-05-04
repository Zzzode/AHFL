#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderWriteRetryDecisionFormatVersion =
    "ahfl.durable-store-import-provider-write-retry-decision.v1";

inline constexpr std::string_view kProviderWriteRetryTokenVersion =
    "ahfl.provider-write-retry-token.v1";

enum class ProviderWriteRetryEligibility {
    Retryable,
    NonRetryable,
    DuplicateReviewRequired,
    NotApplicable,
    Blocked,
};

enum class ProviderWriteRetryFailureKind {
    AdapterResultNotReady,
    RetryableProviderFailure,
    NonRetryableProviderFailure,
    DuplicateWriteSuspected,
};

struct ProviderWriteRetryFailureAttribution {
    ProviderWriteRetryFailureKind kind{ProviderWriteRetryFailureKind::AdapterResultNotReady};
    std::string message;
};

struct ProviderWriteRetryDecision {
    std::string format_version{std::string(kProviderWriteRetryDecisionFormatVersion)};
    std::string
        source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version{
            std::string(kProviderSdkMockAdapterExecutionResultFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_mock_adapter_result_identity;
    ProviderSdkMockAdapterNormalizedResultKind source_normalized_result{
        ProviderSdkMockAdapterNormalizedResultKind::Blocked};
    std::string durable_store_import_provider_write_retry_decision_identity;
    std::string idempotency_key_namespace{"ahfl.durable-store-import.provider-write.v1"};
    std::string idempotency_key;
    std::string retry_token_version{std::string(kProviderWriteRetryTokenVersion)};
    std::string retry_token_placeholder_identity;
    ProviderWriteRetryEligibility retry_eligibility{ProviderWriteRetryEligibility::Blocked};
    bool retry_allowed{false};
    bool duplicate_write_possible{false};
    std::string duplicate_detection_summary;
    std::string retry_decision_summary;
    std::optional<ProviderWriteRetryFailureAttribution> failure_attribution;
};

struct ProviderWriteRetryDecisionValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRetryDecisionResult {
    std::optional<ProviderWriteRetryDecision> decision;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderWriteRetryDecisionValidationResult
validate_provider_write_retry_decision(const ProviderWriteRetryDecision &decision);

[[nodiscard]] ProviderWriteRetryDecisionResult
build_provider_write_retry_decision(const ProviderSdkMockAdapterExecutionResult &adapter_result);

} // namespace ahfl::durable_store_import

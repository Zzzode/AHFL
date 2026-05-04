#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk_payload.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderSdkMockAdapterContractFormatVersion =
    "ahfl.durable-store-import-provider-sdk-mock-adapter-contract.v1";

inline constexpr std::string_view kProviderSdkMockAdapterExecutionResultFormatVersion =
    "ahfl.durable-store-import-provider-sdk-mock-adapter-execution-result.v1";

inline constexpr std::string_view kProviderSdkMockAdapterReadinessFormatVersion =
    "ahfl.durable-store-import-provider-sdk-mock-adapter-readiness.v1";

enum class ProviderSdkMockAdapterOperationKind {
    PlanMockAdapterContract,
    RunMockAdapter,
    NoopPayloadNotReady,
    NoopContractNotReady,
};

enum class ProviderSdkMockAdapterStatus {
    Ready,
    Blocked,
};

enum class ProviderSdkMockAdapterScenarioKind {
    Success,
    Failure,
    Timeout,
    Throttle,
    Conflict,
    SchemaMismatch,
};

enum class ProviderSdkMockAdapterNormalizedResultKind {
    Accepted,
    ProviderFailure,
    Timeout,
    Throttled,
    Conflict,
    SchemaMismatch,
    Blocked,
};

enum class ProviderSdkMockAdapterFailureKind {
    PayloadNotReady,
    ContractNotReady,
    ProviderFailure,
    Timeout,
    Throttle,
    Conflict,
    SchemaMismatch,
};

enum class ProviderSdkMockAdapterNextActionKind {
    ReadyForRealAdapterParity,
    WaitForPayload,
    ManualReviewRequired,
};

struct ProviderSdkMockAdapterFailureAttribution {
    ProviderSdkMockAdapterFailureKind kind{ProviderSdkMockAdapterFailureKind::PayloadNotReady};
    std::string message;
};

struct ProviderSdkMockAdapterContract {
    std::string format_version{std::string(kProviderSdkMockAdapterContractFormatVersion)};
    std::string source_durable_store_import_provider_sdk_payload_audit_summary_format_version{
        std::string(kProviderSdkPayloadAuditSummaryFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_payload_plan_identity;
    ProviderSdkPayloadNextActionKind source_payload_next_action{
        ProviderSdkPayloadNextActionKind::ManualReviewRequired};
    std::string durable_store_import_provider_sdk_mock_adapter_contract_identity;
    ProviderSdkMockAdapterOperationKind operation_kind{
        ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady};
    ProviderSdkMockAdapterStatus contract_status{ProviderSdkMockAdapterStatus::Blocked};
    ProviderSdkMockAdapterScenarioKind scenario_kind{ProviderSdkMockAdapterScenarioKind::Success};
    std::string provider_request_payload_schema_ref{
        std::string(kProviderFakeSdkPayloadSchemaVersion)};
    std::string payload_digest;
    bool fake_provider_only{true};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool materializes_credential_material{false};
    bool invokes_real_provider_sdk{false};
    std::optional<ProviderSdkMockAdapterFailureAttribution> failure_attribution;
};

struct ProviderSdkMockAdapterExecutionResult {
    std::string format_version{std::string(kProviderSdkMockAdapterExecutionResultFormatVersion)};
    std::string source_durable_store_import_provider_sdk_mock_adapter_contract_format_version{
        std::string(kProviderSdkMockAdapterContractFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_mock_adapter_contract_identity;
    ProviderSdkMockAdapterStatus source_contract_status{ProviderSdkMockAdapterStatus::Blocked};
    std::string durable_store_import_provider_sdk_mock_adapter_result_identity;
    ProviderSdkMockAdapterOperationKind operation_kind{
        ProviderSdkMockAdapterOperationKind::NoopContractNotReady};
    ProviderSdkMockAdapterStatus result_status{ProviderSdkMockAdapterStatus::Blocked};
    ProviderSdkMockAdapterScenarioKind scenario_kind{ProviderSdkMockAdapterScenarioKind::Success};
    ProviderSdkMockAdapterNormalizedResultKind normalized_result{
        ProviderSdkMockAdapterNormalizedResultKind::Blocked};
    int provider_status_code{0};
    std::string normalized_message;
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool materializes_credential_material{false};
    bool invokes_real_provider_sdk{false};
    std::optional<ProviderSdkMockAdapterFailureAttribution> failure_attribution;
};

struct ProviderSdkMockAdapterReadiness {
    std::string format_version{std::string(kProviderSdkMockAdapterReadinessFormatVersion)};
    std::string
        source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version{
            std::string(kProviderSdkMockAdapterExecutionResultFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_mock_adapter_result_identity;
    ProviderSdkMockAdapterStatus result_status{ProviderSdkMockAdapterStatus::Blocked};
    ProviderSdkMockAdapterNormalizedResultKind normalized_result{
        ProviderSdkMockAdapterNormalizedResultKind::Blocked};
    std::string normalization_summary;
    std::string next_step_recommendation;
    ProviderSdkMockAdapterNextActionKind next_action{
        ProviderSdkMockAdapterNextActionKind::ManualReviewRequired};
    std::optional<ProviderSdkMockAdapterFailureAttribution> failure_attribution;
};

struct ProviderSdkMockAdapterContractValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkMockAdapterResultValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkMockAdapterReadinessValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkMockAdapterContractResult {
    std::optional<ProviderSdkMockAdapterContract> contract;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkMockAdapterExecutionResultResult {
    std::optional<ProviderSdkMockAdapterExecutionResult> result;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkMockAdapterReadinessResult {
    std::optional<ProviderSdkMockAdapterReadiness> readiness;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSdkMockAdapterContractValidationResult
validate_provider_sdk_mock_adapter_contract(const ProviderSdkMockAdapterContract &contract);

[[nodiscard]] ProviderSdkMockAdapterResultValidationResult
validate_provider_sdk_mock_adapter_execution_result(
    const ProviderSdkMockAdapterExecutionResult &result);

[[nodiscard]] ProviderSdkMockAdapterReadinessValidationResult
validate_provider_sdk_mock_adapter_readiness(const ProviderSdkMockAdapterReadiness &readiness);

[[nodiscard]] ProviderSdkMockAdapterContractResult
build_provider_sdk_mock_adapter_contract(const ProviderSdkPayloadAuditSummary &audit);

[[nodiscard]] ProviderSdkMockAdapterExecutionResultResult
run_provider_sdk_mock_adapter(const ProviderSdkMockAdapterContract &contract);

[[nodiscard]] ProviderSdkMockAdapterReadinessResult
build_provider_sdk_mock_adapter_readiness(const ProviderSdkMockAdapterExecutionResult &result);

} // namespace ahfl::durable_store_import

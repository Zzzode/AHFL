#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_local_host_execution.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderSdkAdapterRequestPlanFormatVersion =
    "ahfl.durable-store-import-provider-sdk-adapter-request-plan.v1";

inline constexpr std::string_view kProviderSdkAdapterResponsePlaceholderFormatVersion =
    "ahfl.durable-store-import-provider-sdk-adapter-response-placeholder.v1";

inline constexpr std::string_view kProviderSdkAdapterReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-sdk-adapter-readiness-review.v1";

enum class ProviderSdkAdapterOperationKind {
    PlanProviderSdkAdapterRequest,
    PlanProviderSdkAdapterResponsePlaceholder,
    NoopLocalHostExecutionNotReady,
    NoopSdkAdapterRequestNotReady,
};

enum class ProviderSdkAdapterStatus {
    Ready,
    Blocked,
};

enum class ProviderSdkAdapterFailureKind {
    LocalHostExecutionNotReady,
    SdkAdapterRequestNotReady,
};

enum class ProviderSdkAdapterReadinessNextActionKind {
    ReadyForProviderSdkAdapterInterface,
    WaitForLocalHostExecutionReceipt,
    ManualReviewRequired,
};

struct ProviderSdkAdapterFailureAttribution {
    ProviderSdkAdapterFailureKind kind{ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady};
    std::string message;
};

struct ProviderSdkAdapterRequestPlan {
    std::string format_version{std::string(kProviderSdkAdapterRequestPlanFormatVersion)};
    std::string source_durable_store_import_provider_local_host_execution_receipt_format_version{
        std::string(kProviderLocalHostExecutionReceiptFormatVersion)};
    std::string source_durable_store_import_provider_host_execution_plan_format_version{
        std::string(kProviderHostExecutionPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_request_envelope_identity;
    std::string durable_store_import_provider_host_execution_identity;
    std::string durable_store_import_provider_local_host_execution_receipt_identity;
    ProviderLocalHostExecutionStatus source_local_host_execution_status{
        ProviderLocalHostExecutionStatus::Blocked};
    std::optional<std::string> source_provider_local_host_execution_receipt_identity;
    std::string durable_store_import_provider_sdk_adapter_request_identity;
    ProviderSdkAdapterOperationKind operation_kind{
        ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady};
    ProviderSdkAdapterStatus request_status{ProviderSdkAdapterStatus::Blocked};
    std::optional<std::string> provider_sdk_adapter_request_identity;
    std::optional<std::string> provider_sdk_adapter_response_placeholder_identity;
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> object_path;
    std::optional<std::string> database_table;
    std::optional<std::string> sdk_request_payload;
    std::optional<std::string> sdk_response_payload;
    std::optional<ProviderSdkAdapterFailureAttribution> failure_attribution;
};

struct ProviderSdkAdapterResponsePlaceholder {
    std::string format_version{std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion)};
    std::string source_durable_store_import_provider_sdk_adapter_request_plan_format_version{
        std::string(kProviderSdkAdapterRequestPlanFormatVersion)};
    std::string source_durable_store_import_provider_local_host_execution_receipt_format_version{
        std::string(kProviderLocalHostExecutionReceiptFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_adapter_request_identity;
    std::string durable_store_import_provider_local_host_execution_receipt_identity;
    ProviderSdkAdapterStatus source_request_status{ProviderSdkAdapterStatus::Blocked};
    std::optional<std::string> source_provider_sdk_adapter_request_identity;
    std::string durable_store_import_provider_sdk_adapter_response_placeholder_identity;
    ProviderSdkAdapterOperationKind operation_kind{
        ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady};
    ProviderSdkAdapterStatus response_status{ProviderSdkAdapterStatus::Blocked};
    std::optional<std::string> provider_sdk_adapter_response_placeholder_identity;
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderSdkAdapterFailureAttribution> failure_attribution;
};

struct ProviderSdkAdapterReadinessReview {
    std::string format_version{std::string(kProviderSdkAdapterReadinessReviewFormatVersion)};
    std::string
        source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version{
            std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion)};
    std::string source_durable_store_import_provider_sdk_adapter_request_plan_format_version{
        std::string(kProviderSdkAdapterRequestPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_adapter_request_identity;
    std::string durable_store_import_provider_sdk_adapter_response_placeholder_identity;
    ProviderSdkAdapterStatus response_status{ProviderSdkAdapterStatus::Blocked};
    ProviderSdkAdapterOperationKind operation_kind{
        ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady};
    std::optional<std::string> provider_sdk_adapter_response_placeholder_identity;
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderSdkAdapterFailureAttribution> failure_attribution;
    std::string sdk_adapter_boundary_summary;
    std::string next_step_recommendation;
    ProviderSdkAdapterReadinessNextActionKind next_action{
        ProviderSdkAdapterReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderSdkAdapterRequestPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterResponsePlaceholderValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterReadinessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterRequestPlanResult {
    std::optional<ProviderSdkAdapterRequestPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterResponsePlaceholderResult {
    std::optional<ProviderSdkAdapterResponsePlaceholder> placeholder;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterReadinessReviewResult {
    std::optional<ProviderSdkAdapterReadinessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSdkAdapterRequestPlanValidationResult
validate_provider_sdk_adapter_request_plan(const ProviderSdkAdapterRequestPlan &plan);

[[nodiscard]] ProviderSdkAdapterResponsePlaceholderValidationResult
validate_provider_sdk_adapter_response_placeholder(
    const ProviderSdkAdapterResponsePlaceholder &placeholder);

[[nodiscard]] ProviderSdkAdapterReadinessReviewValidationResult
validate_provider_sdk_adapter_readiness_review(const ProviderSdkAdapterReadinessReview &review);

[[nodiscard]] ProviderSdkAdapterRequestPlanResult
build_provider_sdk_adapter_request_plan(const ProviderLocalHostExecutionReceipt &receipt);

[[nodiscard]] ProviderSdkAdapterResponsePlaceholderResult
build_provider_sdk_adapter_response_placeholder(const ProviderSdkAdapterRequestPlan &plan);

[[nodiscard]] ProviderSdkAdapterReadinessReviewResult build_provider_sdk_adapter_readiness_review(
    const ProviderSdkAdapterResponsePlaceholder &placeholder);

} // namespace ahfl::durable_store_import

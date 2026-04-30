#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderHostExecutionPolicyFormatVersion =
    "ahfl.durable-store-import-provider-host-execution-policy.v1";

inline constexpr std::string_view kProviderHostExecutionPlanFormatVersion =
    "ahfl.durable-store-import-provider-host-execution-plan.v1";

inline constexpr std::string_view kProviderHostExecutionReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-host-execution-readiness-review.v1";

enum class ProviderHostExecutionCapabilityKind {
    PlanLocalHostExecutionDescriptor,
    PlanDryRunHostReceiptPlaceholder,
};

enum class ProviderHostExecutionOperationKind {
    PlanProviderHostExecution,
    NoopSdkHandoffNotReady,
    NoopHostPolicyMismatch,
    NoopUnsupportedHostCapability,
};

enum class ProviderHostExecutionStatus {
    Ready,
    Blocked,
};

enum class ProviderHostExecutionFailureKind {
    SdkHandoffNotReady,
    HostPolicyMismatch,
    UnsupportedHostCapability,
};

enum class ProviderHostExecutionReadinessNextActionKind {
    ReadyForLocalHostExecutionHarness,
    FixHostPolicy,
    WaitForHostCapability,
    ManualReviewRequired,
};

struct ProviderHostExecutionPolicy {
    std::string format_version{std::string(kProviderHostExecutionPolicyFormatVersion)};
    std::string host_execution_policy_identity;
    std::string host_handoff_policy_ref;
    std::string provider_namespace;
    std::string execution_profile_ref;
    std::string sandbox_policy_ref;
    std::string timeout_policy_ref;
    bool credential_free{true};
    bool network_free{true};
    bool supports_local_host_execution_descriptor_planning{true};
    bool supports_dry_run_host_receipt_placeholder_planning{true};
    std::optional<std::string> credential_reference;
    std::optional<std::string> secret_value;
    std::optional<std::string> host_command;
    std::optional<std::string> host_environment_secret;
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> network_endpoint_uri;
    std::optional<std::string> object_path;
    std::optional<std::string> database_table;
    std::optional<std::string> sdk_request_payload;
    std::optional<std::string> sdk_response_payload;
};

struct ProviderHostExecutionFailureAttribution {
    ProviderHostExecutionFailureKind kind{ProviderHostExecutionFailureKind::SdkHandoffNotReady};
    std::string message;
    std::optional<ProviderHostExecutionCapabilityKind> missing_capability;
};

struct ProviderHostExecutionPlan {
    std::string format_version{std::string(kProviderHostExecutionPlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_request_envelope_plan_format_version{
        std::string(kProviderSdkRequestEnvelopePlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_envelope_policy_format_version{
        std::string(kProviderSdkEnvelopePolicyFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_request_envelope_identity;
    std::optional<std::string> source_host_handoff_descriptor_identity;
    ProviderSdkEnvelopeStatus source_envelope_status{ProviderSdkEnvelopeStatus::Blocked};
    ProviderHostExecutionPolicy host_execution_policy;
    std::string durable_store_import_provider_host_execution_identity;
    ProviderHostExecutionOperationKind operation_kind{
        ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady};
    ProviderHostExecutionStatus execution_status{ProviderHostExecutionStatus::Blocked};
    std::optional<std::string> provider_host_execution_descriptor_identity;
    std::optional<std::string> provider_host_receipt_placeholder_identity;
    bool starts_host_process{false};
    bool reads_host_environment{false};
    bool opens_network_connection{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderHostExecutionFailureAttribution> failure_attribution;
};

struct ProviderHostExecutionReadinessReview {
    std::string format_version{std::string(kProviderHostExecutionReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_host_execution_plan_format_version{
        std::string(kProviderHostExecutionPlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_request_envelope_plan_format_version{
        std::string(kProviderSdkRequestEnvelopePlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_request_envelope_identity;
    std::string durable_store_import_provider_host_execution_identity;
    ProviderHostExecutionStatus execution_status{ProviderHostExecutionStatus::Blocked};
    ProviderHostExecutionOperationKind operation_kind{
        ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady};
    std::optional<std::string> provider_host_execution_descriptor_identity;
    std::optional<std::string> provider_host_receipt_placeholder_identity;
    bool starts_host_process{false};
    bool reads_host_environment{false};
    bool opens_network_connection{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderHostExecutionFailureAttribution> failure_attribution;
    std::string host_execution_boundary_summary;
    std::string next_step_recommendation;
    ProviderHostExecutionReadinessNextActionKind next_action{
        ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderHostExecutionPolicyValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderHostExecutionPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderHostExecutionReadinessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderHostExecutionPlanResult {
    std::optional<ProviderHostExecutionPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderHostExecutionReadinessReviewResult {
    std::optional<ProviderHostExecutionReadinessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderHostExecutionPolicyValidationResult
validate_provider_host_execution_policy(const ProviderHostExecutionPolicy &policy);

[[nodiscard]] ProviderHostExecutionPlanValidationResult
validate_provider_host_execution_plan(const ProviderHostExecutionPlan &plan);

[[nodiscard]] ProviderHostExecutionReadinessReviewValidationResult
validate_provider_host_execution_readiness_review(
    const ProviderHostExecutionReadinessReview &review);

[[nodiscard]] ProviderHostExecutionPolicy
build_default_provider_host_execution_policy(const ProviderSdkRequestEnvelopePlan &plan);

[[nodiscard]] ProviderHostExecutionPlanResult
build_provider_host_execution_plan(const ProviderSdkRequestEnvelopePlan &plan);

[[nodiscard]] ProviderHostExecutionPlanResult
build_provider_host_execution_plan(const ProviderSdkRequestEnvelopePlan &plan,
                                   const ProviderHostExecutionPolicy &policy);

[[nodiscard]] ProviderHostExecutionReadinessReviewResult
build_provider_host_execution_readiness_review(const ProviderHostExecutionPlan &plan);

} // namespace ahfl::durable_store_import

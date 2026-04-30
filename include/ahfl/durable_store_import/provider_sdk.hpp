#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_runtime.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderSdkEnvelopePolicyFormatVersion =
    "ahfl.durable-store-import-provider-sdk-envelope-policy.v1";

inline constexpr std::string_view kProviderSdkRequestEnvelopePlanFormatVersion =
    "ahfl.durable-store-import-provider-sdk-request-envelope-plan.v1";

inline constexpr std::string_view kProviderSdkHandoffReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-sdk-handoff-readiness-review.v1";

enum class ProviderSdkEnvelopeCapabilityKind {
    PlanSecretFreeRequestEnvelope,
    PlanHostHandoffDescriptor,
};

enum class ProviderSdkEnvelopeOperationKind {
    PlanProviderSdkRequestEnvelope,
    NoopRuntimePreflightNotReady,
    NoopEnvelopePolicyMismatch,
    NoopUnsupportedEnvelopeCapability,
};

enum class ProviderSdkEnvelopeStatus {
    Ready,
    Blocked,
};

enum class ProviderSdkEnvelopeFailureKind {
    RuntimePreflightNotReady,
    EnvelopePolicyMismatch,
    UnsupportedEnvelopeCapability,
};

enum class ProviderSdkHandoffReadinessNextActionKind {
    ReadyForHostExecutionPrototype,
    FixEnvelopePolicy,
    WaitForEnvelopeCapability,
    ManualReviewRequired,
};

struct ProviderSdkEnvelopePolicy {
    std::string format_version{std::string(kProviderSdkEnvelopePolicyFormatVersion)};
    std::string sdk_envelope_policy_identity;
    std::string runtime_profile_ref;
    std::string provider_namespace;
    std::string secret_free_request_schema_ref;
    std::string host_handoff_policy_ref;
    bool credential_free{true};
    bool supports_secret_free_request_envelope_planning{true};
    bool supports_host_handoff_descriptor_planning{true};
    std::optional<std::string> credential_reference;
    std::optional<std::string> secret_value;
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> object_path;
    std::optional<std::string> database_table;
    std::optional<std::string> sdk_request_payload;
    std::optional<std::string> sdk_response_payload;
    std::optional<std::string> host_command;
    std::optional<std::string> network_endpoint_uri;
};

struct ProviderSdkEnvelopeFailureAttribution {
    ProviderSdkEnvelopeFailureKind kind{ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady};
    std::string message;
    std::optional<ProviderSdkEnvelopeCapabilityKind> missing_capability;
};

struct ProviderSdkRequestEnvelopePlan {
    std::string format_version{std::string(kProviderSdkRequestEnvelopePlanFormatVersion)};
    std::string source_durable_store_import_provider_runtime_preflight_plan_format_version{
        std::string(kProviderRuntimePreflightPlanFormatVersion)};
    std::string source_durable_store_import_provider_runtime_profile_format_version{
        std::string(kProviderRuntimeProfileFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_runtime_preflight_identity;
    std::optional<std::string> source_sdk_invocation_envelope_identity;
    ProviderRuntimePreflightStatus source_preflight_status{ProviderRuntimePreflightStatus::Blocked};
    ProviderSdkEnvelopePolicy envelope_policy;
    std::string durable_store_import_provider_sdk_request_envelope_identity;
    ProviderSdkEnvelopeOperationKind operation_kind{
        ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady};
    ProviderSdkEnvelopeStatus envelope_status{ProviderSdkEnvelopeStatus::Blocked};
    std::optional<std::string> provider_sdk_request_envelope_identity;
    std::optional<std::string> host_handoff_descriptor_identity;
    bool materializes_sdk_request_payload{false};
    bool starts_host_process{false};
    bool opens_network_connection{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderSdkEnvelopeFailureAttribution> failure_attribution;
};

struct ProviderSdkHandoffReadinessReview {
    std::string format_version{std::string(kProviderSdkHandoffReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_sdk_request_envelope_plan_format_version{
        std::string(kProviderSdkRequestEnvelopePlanFormatVersion)};
    std::string source_durable_store_import_provider_runtime_preflight_plan_format_version{
        std::string(kProviderRuntimePreflightPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_runtime_preflight_identity;
    std::string durable_store_import_provider_sdk_request_envelope_identity;
    ProviderSdkEnvelopeStatus envelope_status{ProviderSdkEnvelopeStatus::Blocked};
    ProviderSdkEnvelopeOperationKind operation_kind{
        ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady};
    std::optional<std::string> provider_sdk_request_envelope_identity;
    std::optional<std::string> host_handoff_descriptor_identity;
    bool materializes_sdk_request_payload{false};
    bool starts_host_process{false};
    bool opens_network_connection{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderSdkEnvelopeFailureAttribution> failure_attribution;
    std::string sdk_boundary_summary;
    std::string next_step_recommendation;
    ProviderSdkHandoffReadinessNextActionKind next_action{
        ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderSdkEnvelopePolicyValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkRequestEnvelopePlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkHandoffReadinessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkRequestEnvelopePlanResult {
    std::optional<ProviderSdkRequestEnvelopePlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkHandoffReadinessReviewResult {
    std::optional<ProviderSdkHandoffReadinessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSdkEnvelopePolicyValidationResult
validate_provider_sdk_envelope_policy(const ProviderSdkEnvelopePolicy &policy);

[[nodiscard]] ProviderSdkRequestEnvelopePlanValidationResult
validate_provider_sdk_request_envelope_plan(const ProviderSdkRequestEnvelopePlan &plan);

[[nodiscard]] ProviderSdkHandoffReadinessReviewValidationResult
validate_provider_sdk_handoff_readiness_review(const ProviderSdkHandoffReadinessReview &review);

[[nodiscard]] ProviderSdkEnvelopePolicy
build_default_provider_sdk_envelope_policy(const ProviderRuntimePreflightPlan &plan);

[[nodiscard]] ProviderSdkRequestEnvelopePlanResult
build_provider_sdk_request_envelope_plan(const ProviderRuntimePreflightPlan &plan);

[[nodiscard]] ProviderSdkRequestEnvelopePlanResult
build_provider_sdk_request_envelope_plan(const ProviderRuntimePreflightPlan &plan,
                                         const ProviderSdkEnvelopePolicy &policy);

[[nodiscard]] ProviderSdkHandoffReadinessReviewResult
build_provider_sdk_handoff_readiness_review(const ProviderSdkRequestEnvelopePlan &plan);

} // namespace ahfl::durable_store_import

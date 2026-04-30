#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_driver.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderRuntimeProfileFormatVersion =
    "ahfl.durable-store-import-provider-runtime-profile.v1";

inline constexpr std::string_view kProviderRuntimePreflightPlanFormatVersion =
    "ahfl.durable-store-import-provider-runtime-preflight-plan.v1";

inline constexpr std::string_view kProviderRuntimeReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-runtime-readiness-review.v1";

enum class ProviderRuntimeCapabilityKind {
    LoadSecretFreeRuntimeProfile,
    ResolveSecretHandlePlaceholder,
    LoadConfigSnapshotPlaceholder,
    PlanSdkInvocationEnvelope,
};

enum class ProviderRuntimeOperationKind {
    PlanProviderSdkInvocationEnvelope,
    NoopDriverBindingNotReady,
    NoopRuntimeProfileMismatch,
    NoopUnsupportedRuntimeCapability,
};

enum class ProviderRuntimePreflightStatus {
    Ready,
    Blocked,
};

enum class ProviderRuntimePreflightFailureKind {
    DriverBindingNotReady,
    RuntimeProfileMismatch,
    UnsupportedRuntimeCapability,
};

enum class ProviderRuntimeReadinessNextActionKind {
    ReadyForSdkAdapterImplementation,
    FixRuntimeProfile,
    WaitForRuntimeCapability,
    ManualReviewRequired,
};

struct ProviderRuntimeProfile {
    std::string format_version{std::string(kProviderRuntimeProfileFormatVersion)};
    std::string runtime_profile_identity;
    std::string driver_profile_ref;
    std::string provider_namespace;
    std::string secret_free_runtime_config_ref;
    std::string secret_resolver_policy_ref;
    std::string config_snapshot_policy_ref;
    bool credential_free{true};
    bool supports_secret_free_runtime_profile_load{true};
    bool supports_secret_handle_placeholder_resolution{true};
    bool supports_config_snapshot_placeholder_load{true};
    bool supports_sdk_invocation_envelope_planning{true};
    std::optional<std::string> credential_reference;
    std::optional<std::string> secret_value;
    std::optional<std::string> secret_manager_endpoint_uri;
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> object_path;
    std::optional<std::string> database_table;
    std::optional<std::string> sdk_payload_schema;
    std::optional<std::string> sdk_request_payload;
};

struct ProviderRuntimePreflightFailureAttribution {
    ProviderRuntimePreflightFailureKind kind{
        ProviderRuntimePreflightFailureKind::DriverBindingNotReady};
    std::string message;
    std::optional<ProviderRuntimeCapabilityKind> missing_capability;
};

struct ProviderRuntimePreflightPlan {
    std::string format_version{std::string(kProviderRuntimePreflightPlanFormatVersion)};
    std::string source_durable_store_import_provider_driver_binding_plan_format_version{
        std::string(kProviderDriverBindingPlanFormatVersion)};
    std::string source_durable_store_import_provider_driver_profile_format_version{
        std::string(kProviderDriverProfileFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_driver_binding_identity;
    std::optional<std::string> provider_persistence_id;
    ProviderDriverBindingStatus source_binding_status{ProviderDriverBindingStatus::NotBound};
    std::optional<std::string> source_operation_descriptor_identity;
    ProviderRuntimeProfile runtime_profile;
    std::string durable_store_import_provider_runtime_preflight_identity;
    ProviderRuntimeOperationKind operation_kind{
        ProviderRuntimeOperationKind::NoopDriverBindingNotReady};
    ProviderRuntimePreflightStatus preflight_status{ProviderRuntimePreflightStatus::Blocked};
    std::optional<std::string> sdk_invocation_envelope_identity;
    bool loads_runtime_config{false};
    bool resolves_secret_handles{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderRuntimePreflightFailureAttribution> failure_attribution;
};

struct ProviderRuntimeReadinessReview {
    std::string format_version{std::string(kProviderRuntimeReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_runtime_preflight_plan_format_version{
        std::string(kProviderRuntimePreflightPlanFormatVersion)};
    std::string source_durable_store_import_provider_driver_binding_plan_format_version{
        std::string(kProviderDriverBindingPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_driver_binding_identity;
    std::string durable_store_import_provider_runtime_preflight_identity;
    ProviderRuntimePreflightStatus preflight_status{ProviderRuntimePreflightStatus::Blocked};
    ProviderRuntimeOperationKind operation_kind{
        ProviderRuntimeOperationKind::NoopDriverBindingNotReady};
    std::optional<std::string> sdk_invocation_envelope_identity;
    bool loads_runtime_config{false};
    bool resolves_secret_handles{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderRuntimePreflightFailureAttribution> failure_attribution;
    std::string runtime_boundary_summary;
    std::string next_step_recommendation;
    ProviderRuntimeReadinessNextActionKind next_action{
        ProviderRuntimeReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderRuntimeProfileValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRuntimePreflightPlanValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRuntimeReadinessReviewValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRuntimePreflightPlanResult {
    std::optional<ProviderRuntimePreflightPlan> plan;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRuntimeReadinessReviewResult {
    std::optional<ProviderRuntimeReadinessReview> review;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderRuntimeProfileValidationResult
validate_provider_runtime_profile(const ProviderRuntimeProfile &profile);

[[nodiscard]] ProviderRuntimePreflightPlanValidationResult
validate_provider_runtime_preflight_plan(const ProviderRuntimePreflightPlan &plan);

[[nodiscard]] ProviderRuntimeReadinessReviewValidationResult
validate_provider_runtime_readiness_review(const ProviderRuntimeReadinessReview &review);

[[nodiscard]] ProviderRuntimeProfile
build_default_provider_runtime_profile(const ProviderDriverBindingPlan &plan);

[[nodiscard]] ProviderRuntimePreflightPlanResult
build_provider_runtime_preflight_plan(const ProviderDriverBindingPlan &plan);

[[nodiscard]] ProviderRuntimePreflightPlanResult
build_provider_runtime_preflight_plan(const ProviderDriverBindingPlan &plan,
                                      const ProviderRuntimeProfile &profile);

[[nodiscard]] ProviderRuntimeReadinessReviewResult
build_provider_runtime_readiness_review(const ProviderRuntimePreflightPlan &plan);

} // namespace ahfl::durable_store_import

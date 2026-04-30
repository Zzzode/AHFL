#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_adapter.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderDriverProfileFormatVersion =
    "ahfl.durable-store-import-provider-driver-profile.v1";

inline constexpr std::string_view kProviderDriverBindingPlanFormatVersion =
    "ahfl.durable-store-import-provider-driver-binding-plan.v1";

inline constexpr std::string_view kProviderDriverReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-driver-readiness-review.v1";

enum class ProviderDriverKind {
    ProviderNeutralPreviewDriver,
    AwsObjectStorePreviewDriver,
    GcpObjectStorePreviewDriver,
    AzureObjectStorePreviewDriver,
};

enum class ProviderDriverCapabilityKind {
    LoadSecretFreeProfile,
    TranslateWriteIntent,
    TranslateRetryPlaceholder,
    TranslateResumePlaceholder,
};

enum class ProviderDriverOperationKind {
    TranslateProviderPersistReceipt,
    NoopSourceNotPlanned,
    NoopProfileMismatch,
    NoopUnsupportedDriverCapability,
};

enum class ProviderDriverBindingStatus {
    Bound,
    NotBound,
};

enum class ProviderDriverBindingFailureKind {
    SourceWriteAttemptNotPlanned,
    DriverProfileMismatch,
    UnsupportedDriverCapability,
};

enum class ProviderDriverReadinessNextActionKind {
    ReadyForDriverImplementation,
    FixProviderProfile,
    WaitForDriverCapability,
    ManualReviewRequired,
};

struct ProviderDriverProfile {
    std::string format_version{std::string(kProviderDriverProfileFormatVersion)};
    ProviderDriverKind driver_kind{ProviderDriverKind::ProviderNeutralPreviewDriver};
    std::string driver_profile_identity;
    std::string provider_profile_ref;
    std::string provider_namespace;
    std::string secret_free_config_ref;
    bool credential_free{true};
    bool supports_secret_free_profile_load{true};
    bool supports_write_intent_translation{true};
    bool supports_retry_placeholder_translation{true};
    bool supports_resume_placeholder_translation{true};
    std::optional<std::string> credential_reference;
    std::optional<std::string> endpoint_uri;
    std::optional<std::string> endpoint_secret_reference;
    std::optional<std::string> object_path;
    std::optional<std::string> database_table;
    std::optional<std::string> sdk_payload_schema;
};

struct ProviderDriverBindingFailureAttribution {
    ProviderDriverBindingFailureKind kind{
        ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned};
    std::string message;
    std::optional<ProviderDriverCapabilityKind> missing_capability;
};

struct ProviderDriverBindingPlan {
    std::string format_version{std::string(kProviderDriverBindingPlanFormatVersion)};
    std::string source_durable_store_import_provider_write_attempt_preview_format_version{
        std::string(kProviderWriteAttemptPreviewFormatVersion)};
    std::string source_durable_store_import_provider_adapter_config_format_version{
        std::string(kProviderAdapterConfigFormatVersion)};
    std::string source_durable_store_import_provider_capability_matrix_format_version{
        std::string(kProviderCapabilityMatrixFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_attempt_identity;
    std::string durable_store_import_provider_driver_binding_identity;
    ProviderWritePlanningStatus source_planning_status{ProviderWritePlanningStatus::NotPlanned};
    std::optional<std::string> provider_persistence_id;
    ProviderDriverProfile driver_profile;
    ProviderDriverOperationKind operation_kind{ProviderDriverOperationKind::NoopSourceNotPlanned};
    ProviderDriverBindingStatus binding_status{ProviderDriverBindingStatus::NotBound};
    std::optional<std::string> operation_descriptor_identity;
    bool invokes_provider_sdk{false};
    std::optional<ProviderDriverBindingFailureAttribution> failure_attribution;
};

struct ProviderDriverReadinessReview {
    std::string format_version{std::string(kProviderDriverReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_driver_binding_plan_format_version{
        std::string(kProviderDriverBindingPlanFormatVersion)};
    std::string source_durable_store_import_provider_write_attempt_preview_format_version{
        std::string(kProviderWriteAttemptPreviewFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_attempt_identity;
    std::string durable_store_import_provider_driver_binding_identity;
    ProviderDriverBindingStatus binding_status{ProviderDriverBindingStatus::NotBound};
    ProviderDriverOperationKind operation_kind{ProviderDriverOperationKind::NoopSourceNotPlanned};
    std::optional<std::string> operation_descriptor_identity;
    bool invokes_provider_sdk{false};
    std::optional<ProviderDriverBindingFailureAttribution> failure_attribution;
    std::string driver_boundary_summary;
    std::string next_step_recommendation;
    ProviderDriverReadinessNextActionKind next_action{
        ProviderDriverReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderDriverProfileValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderDriverBindingPlanValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderDriverReadinessReviewValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderDriverBindingPlanResult {
    std::optional<ProviderDriverBindingPlan> plan;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderDriverReadinessReviewResult {
    std::optional<ProviderDriverReadinessReview> review;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderDriverProfileValidationResult
validate_provider_driver_profile(const ProviderDriverProfile &profile);

[[nodiscard]] ProviderDriverBindingPlanValidationResult
validate_provider_driver_binding_plan(const ProviderDriverBindingPlan &plan);

[[nodiscard]] ProviderDriverReadinessReviewValidationResult
validate_provider_driver_readiness_review(const ProviderDriverReadinessReview &review);

[[nodiscard]] ProviderDriverProfile
build_default_provider_driver_profile(const ProviderWriteAttemptPreview &preview);

[[nodiscard]] ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview);

[[nodiscard]] ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview,
                                   const ProviderDriverProfile &profile);

[[nodiscard]] ProviderDriverReadinessReviewResult
build_provider_driver_readiness_review(const ProviderDriverBindingPlan &plan);

} // namespace ahfl::durable_store_import

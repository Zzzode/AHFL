#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_config.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderSecretResolverRequestPlanFormatVersion =
    "ahfl.durable-store-import-provider-secret-resolver-request-plan.v1";

inline constexpr std::string_view kProviderSecretResolverResponsePlaceholderFormatVersion =
    "ahfl.durable-store-import-provider-secret-resolver-response-placeholder.v1";

inline constexpr std::string_view kProviderSecretPolicyReviewFormatVersion =
    "ahfl.durable-store-import-provider-secret-policy-review.v1";

inline constexpr std::string_view kProviderSecretHandleSchemaVersion =
    "ahfl.provider-secret-handle.v1";

inline constexpr std::string_view kProviderCredentialLifecycleVersion =
    "ahfl.provider-credential-lifecycle.v1";

enum class ProviderSecretOperationKind {
    PlanSecretResolverRequest,
    PlanSecretResolverResponsePlaceholder,
    NoopConfigSnapshotNotReady,
    NoopSecretResolverRequestNotReady,
};

enum class ProviderSecretStatus {
    Ready,
    Blocked,
};

enum class ProviderCredentialLifecycleState {
    HandlePlanned,
    PlaceholderPendingResolution,
    Blocked,
};

enum class ProviderSecretFailureKind {
    ConfigSnapshotNotReady,
    SecretResolverRequestNotReady,
    SecretPolicyViolation,
};

enum class ProviderSecretPolicyNextActionKind {
    ReadyForLocalHostHarness,
    WaitForConfigSnapshot,
    ManualReviewRequired,
};

struct ProviderSecretFailureAttribution {
    ProviderSecretFailureKind kind{ProviderSecretFailureKind::ConfigSnapshotNotReady};
    std::string message;
};

struct ProviderSecretHandleReference {
    std::string handle_schema_version{std::string(kProviderSecretHandleSchemaVersion)};
    std::string handle_identity;
    std::string provider_key{"local-placeholder-durable-store"};
    std::string profile_key{"local-placeholder-profile"};
    std::string purpose{"provider_config_credential"};
    bool contains_secret_value{false};
    bool contains_credential_material{false};
    bool contains_token_value{false};
};

struct ProviderSecretResolverRequestPlan {
    std::string format_version{std::string(kProviderSecretResolverRequestPlanFormatVersion)};
    std::string source_durable_store_import_provider_config_snapshot_placeholder_format_version{
        std::string(kProviderConfigSnapshotPlaceholderFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_config_snapshot_identity;
    ProviderConfigStatus source_config_snapshot_status{ProviderConfigStatus::Blocked};
    std::string provider_config_profile_identity;
    std::string provider_config_snapshot_placeholder_identity;
    std::string durable_store_import_provider_secret_resolver_request_identity;
    ProviderSecretOperationKind operation_kind{
        ProviderSecretOperationKind::NoopConfigSnapshotNotReady};
    ProviderSecretStatus request_status{ProviderSecretStatus::Blocked};
    ProviderSecretHandleReference secret_handle;
    std::string provider_secret_resolver_response_placeholder_identity;
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool materializes_token_value{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool invokes_secret_manager{false};
    std::optional<std::string> secret_value;
    std::optional<std::string> credential_material;
    std::optional<std::string> token_value;
    std::optional<std::string> secret_manager_response;
    std::optional<ProviderSecretFailureAttribution> failure_attribution;
};

struct ProviderSecretResolverResponsePlaceholder {
    std::string format_version{
        std::string(kProviderSecretResolverResponsePlaceholderFormatVersion)};
    std::string source_durable_store_import_provider_secret_resolver_request_plan_format_version{
        std::string(kProviderSecretResolverRequestPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_secret_resolver_request_identity;
    ProviderSecretStatus source_secret_resolver_request_status{ProviderSecretStatus::Blocked};
    std::string durable_store_import_provider_secret_resolver_response_identity;
    ProviderSecretOperationKind operation_kind{
        ProviderSecretOperationKind::NoopSecretResolverRequestNotReady};
    ProviderSecretStatus response_status{ProviderSecretStatus::Blocked};
    ProviderSecretHandleReference secret_handle;
    std::string credential_lifecycle_version{std::string(kProviderCredentialLifecycleVersion)};
    ProviderCredentialLifecycleState credential_lifecycle_state{
        ProviderCredentialLifecycleState::Blocked};
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool materializes_token_value{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool invokes_secret_manager{false};
    std::optional<ProviderSecretFailureAttribution> failure_attribution;
};

struct ProviderSecretPolicyReview {
    std::string format_version{std::string(kProviderSecretPolicyReviewFormatVersion)};
    std::string
        source_durable_store_import_provider_secret_resolver_response_placeholder_format_version{
            std::string(kProviderSecretResolverResponsePlaceholderFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_secret_resolver_request_identity;
    std::string durable_store_import_provider_secret_resolver_response_identity;
    ProviderSecretStatus response_status{ProviderSecretStatus::Blocked};
    ProviderSecretOperationKind operation_kind{
        ProviderSecretOperationKind::NoopSecretResolverRequestNotReady};
    ProviderSecretHandleReference secret_handle;
    std::string credential_lifecycle_version{std::string(kProviderCredentialLifecycleVersion)};
    ProviderCredentialLifecycleState credential_lifecycle_state{
        ProviderCredentialLifecycleState::Blocked};
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool materializes_token_value{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool invokes_secret_manager{false};
    std::optional<ProviderSecretFailureAttribution> failure_attribution;
    std::string secret_policy_summary;
    std::string next_step_recommendation;
    ProviderSecretPolicyNextActionKind next_action{
        ProviderSecretPolicyNextActionKind::ManualReviewRequired};
};

struct ProviderSecretResolverRequestPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSecretResolverResponsePlaceholderValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSecretPolicyReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSecretResolverRequestPlanResult {
    std::optional<ProviderSecretResolverRequestPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSecretResolverResponsePlaceholderResult {
    std::optional<ProviderSecretResolverResponsePlaceholder> placeholder;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSecretPolicyReviewResult {
    std::optional<ProviderSecretPolicyReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSecretResolverRequestPlanValidationResult
validate_provider_secret_resolver_request_plan(const ProviderSecretResolverRequestPlan &plan);

[[nodiscard]] ProviderSecretResolverResponsePlaceholderValidationResult
validate_provider_secret_resolver_response_placeholder(
    const ProviderSecretResolverResponsePlaceholder &placeholder);

[[nodiscard]] ProviderSecretPolicyReviewValidationResult
validate_provider_secret_policy_review(const ProviderSecretPolicyReview &review);

[[nodiscard]] ProviderSecretResolverRequestPlanResult
build_provider_secret_resolver_request_plan(const ProviderConfigSnapshotPlaceholder &snapshot);

[[nodiscard]] ProviderSecretResolverResponsePlaceholderResult
build_provider_secret_resolver_response_placeholder(const ProviderSecretResolverRequestPlan &plan);

[[nodiscard]] ProviderSecretPolicyReviewResult
build_provider_secret_policy_review(const ProviderSecretResolverResponsePlaceholder &placeholder);

} // namespace ahfl::durable_store_import

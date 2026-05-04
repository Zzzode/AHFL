#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk_interface.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderConfigLoadPlanFormatVersion =
    "ahfl.durable-store-import-provider-config-load-plan.v1";

inline constexpr std::string_view kProviderConfigSnapshotPlaceholderFormatVersion =
    "ahfl.durable-store-import-provider-config-snapshot-placeholder.v1";

inline constexpr std::string_view kProviderConfigReadinessReviewFormatVersion =
    "ahfl.durable-store-import-provider-config-readiness-review.v1";

inline constexpr std::string_view kProviderConfigSchemaVersion = "ahfl.provider-config-schema.v1";

enum class ProviderConfigOperationKind {
    PlanProviderConfigLoad,
    PlanProviderConfigSnapshotPlaceholder,
    NoopAdapterInterfaceNotReady,
    NoopConfigLoadNotReady,
};

enum class ProviderConfigStatus {
    Ready,
    Blocked,
};

enum class ProviderConfigFailureKind {
    AdapterInterfaceNotReady,
    ConfigLoadNotReady,
    MissingConfigProfile,
    IncompatibleConfigSchema,
    UnsupportedConfigProfile,
};

enum class ProviderConfigReadinessNextActionKind {
    ReadyForSecretHandleResolver,
    WaitForAdapterInterface,
    ManualReviewRequired,
};

struct ProviderConfigFailureAttribution {
    ProviderConfigFailureKind kind{ProviderConfigFailureKind::AdapterInterfaceNotReady};
    std::string message;
};

struct ProviderConfigProfileDescriptor {
    std::string profile_key{"local-placeholder-profile"};
    std::string provider_key{"local-placeholder-durable-store"};
    std::string config_schema_version{std::string(kProviderConfigSchemaVersion)};
    bool requires_secret_handle{true};
    bool materializes_secret_value{false};
    bool contains_endpoint_uri{false};
    bool opens_network_connection{false};
};

struct ProviderConfigLoadPlan {
    std::string format_version{std::string(kProviderConfigLoadPlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_adapter_interface_plan_format_version{
        std::string(kProviderSdkAdapterInterfacePlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_adapter_interface_identity;
    ProviderSdkAdapterInterfaceStatus source_adapter_interface_status{
        ProviderSdkAdapterInterfaceStatus::Blocked};
    std::string provider_registry_identity;
    std::string adapter_descriptor_identity;
    std::string provider_key{"local-placeholder-durable-store"};
    std::string durable_store_import_provider_config_load_identity;
    ProviderConfigOperationKind operation_kind{
        ProviderConfigOperationKind::NoopAdapterInterfaceNotReady};
    ProviderConfigStatus config_load_status{ProviderConfigStatus::Blocked};
    std::string provider_config_profile_identity;
    std::string provider_config_snapshot_placeholder_identity;
    ProviderConfigProfileDescriptor profile_descriptor;
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    std::optional<std::string> secret_value;
    std::optional<std::string> credential_material;
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> remote_config_uri;
    std::optional<std::string> sdk_request_payload;
    std::optional<ProviderConfigFailureAttribution> failure_attribution;
};

struct ProviderConfigSnapshotPlaceholder {
    std::string format_version{std::string(kProviderConfigSnapshotPlaceholderFormatVersion)};
    std::string source_durable_store_import_provider_config_load_plan_format_version{
        std::string(kProviderConfigLoadPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_config_load_identity;
    ProviderConfigStatus source_config_load_status{ProviderConfigStatus::Blocked};
    std::string durable_store_import_provider_config_snapshot_identity;
    ProviderConfigOperationKind operation_kind{ProviderConfigOperationKind::NoopConfigLoadNotReady};
    ProviderConfigStatus snapshot_status{ProviderConfigStatus::Blocked};
    std::string provider_config_profile_identity;
    std::string provider_config_snapshot_placeholder_identity;
    std::string config_schema_version{std::string(kProviderConfigSchemaVersion)};
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderConfigFailureAttribution> failure_attribution;
};

struct ProviderConfigReadinessReview {
    std::string format_version{std::string(kProviderConfigReadinessReviewFormatVersion)};
    std::string source_durable_store_import_provider_config_snapshot_placeholder_format_version{
        std::string(kProviderConfigSnapshotPlaceholderFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_config_load_identity;
    std::string durable_store_import_provider_config_snapshot_identity;
    ProviderConfigStatus snapshot_status{ProviderConfigStatus::Blocked};
    ProviderConfigOperationKind operation_kind{ProviderConfigOperationKind::NoopConfigLoadNotReady};
    std::string provider_config_profile_identity;
    std::string provider_config_snapshot_placeholder_identity;
    std::string config_schema_version{std::string(kProviderConfigSchemaVersion)};
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderConfigFailureAttribution> failure_attribution;
    std::string config_boundary_summary;
    std::string next_step_recommendation;
    ProviderConfigReadinessNextActionKind next_action{
        ProviderConfigReadinessNextActionKind::ManualReviewRequired};
};

struct ProviderConfigLoadPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigSnapshotPlaceholderValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigReadinessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigLoadPlanResult {
    std::optional<ProviderConfigLoadPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigSnapshotPlaceholderResult {
    std::optional<ProviderConfigSnapshotPlaceholder> placeholder;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigReadinessReviewResult {
    std::optional<ProviderConfigReadinessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderConfigLoadPlanValidationResult
validate_provider_config_load_plan(const ProviderConfigLoadPlan &plan);

[[nodiscard]] ProviderConfigSnapshotPlaceholderValidationResult
validate_provider_config_snapshot_placeholder(const ProviderConfigSnapshotPlaceholder &placeholder);

[[nodiscard]] ProviderConfigReadinessReviewValidationResult
validate_provider_config_readiness_review(const ProviderConfigReadinessReview &review);

[[nodiscard]] ProviderConfigLoadPlanResult
build_provider_config_load_plan(const ProviderSdkAdapterInterfacePlan &interface_plan);

[[nodiscard]] ProviderConfigSnapshotPlaceholderResult
build_provider_config_snapshot_placeholder(const ProviderConfigLoadPlan &plan);

[[nodiscard]] ProviderConfigReadinessReviewResult
build_provider_config_readiness_review(const ProviderConfigSnapshotPlaceholder &placeholder);

} // namespace ahfl::durable_store_import

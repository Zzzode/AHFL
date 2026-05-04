#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderSdkAdapterInterfacePlanFormatVersion =
    "ahfl.durable-store-import-provider-sdk-adapter-interface-plan.v1";

inline constexpr std::string_view kProviderSdkAdapterInterfaceReviewFormatVersion =
    "ahfl.durable-store-import-provider-sdk-adapter-interface-review.v1";

inline constexpr std::string_view kProviderSdkAdapterInterfaceAbiVersion =
    "ahfl.provider-sdk-adapter-interface.v1";

inline constexpr std::string_view kProviderSdkAdapterErrorTaxonomyVersion =
    "ahfl.provider-sdk-adapter-error-taxonomy.v1";

class ProviderSdkAdapterInterface {
  public:
    virtual ~ProviderSdkAdapterInterface() = default;

    [[nodiscard]] virtual std::string_view provider_key() const noexcept = 0;
    [[nodiscard]] virtual std::string_view adapter_version() const noexcept = 0;
    [[nodiscard]] virtual bool supports_durable_store_import_write() const noexcept = 0;
};

enum class ProviderSdkAdapterInterfaceOperationKind {
    PlanProviderSdkAdapterInterface,
    NoopSdkAdapterRequestNotReady,
    NoopUnsupportedCapability,
};

enum class ProviderSdkAdapterInterfaceStatus {
    Ready,
    Blocked,
};

enum class ProviderSdkAdapterCapabilitySupportStatus {
    Supported,
    Unsupported,
};

enum class ProviderSdkAdapterNormalizedErrorKind {
    None,
    UnsupportedCapability,
    SdkAdapterRequestNotReady,
};

enum class ProviderSdkAdapterInterfaceFailureKind {
    SdkAdapterRequestNotReady,
    UnsupportedCapability,
};

enum class ProviderSdkAdapterInterfaceReviewNextActionKind {
    ReadyForMockAdapterImplementation,
    WaitForSdkAdapterRequest,
    ManualReviewRequired,
};

struct ProviderSdkAdapterInterfaceFailureAttribution {
    ProviderSdkAdapterInterfaceFailureKind kind{
        ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady};
    std::string message;
};

struct ProviderSdkAdapterCapabilityDescriptor {
    std::string capability_key{"durable_store_import.write"};
    ProviderSdkAdapterCapabilitySupportStatus support_status{
        ProviderSdkAdapterCapabilitySupportStatus::Supported};
    std::string input_contract_format_version{
        std::string(kProviderSdkAdapterRequestPlanFormatVersion)};
    std::string output_contract_format_version{
        std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion)};
    bool supports_placeholder_response{true};
};

struct ProviderSdkAdapterInterfacePlan {
    std::string format_version{std::string(kProviderSdkAdapterInterfacePlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_adapter_request_plan_format_version{
        std::string(kProviderSdkAdapterRequestPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_adapter_request_identity;
    ProviderSdkAdapterStatus source_sdk_adapter_request_status{ProviderSdkAdapterStatus::Blocked};
    std::optional<std::string> source_provider_sdk_adapter_request_identity;
    std::string durable_store_import_provider_sdk_adapter_interface_identity;
    ProviderSdkAdapterInterfaceOperationKind operation_kind{
        ProviderSdkAdapterInterfaceOperationKind::NoopSdkAdapterRequestNotReady};
    ProviderSdkAdapterInterfaceStatus interface_status{ProviderSdkAdapterInterfaceStatus::Blocked};
    std::string provider_registry_identity;
    std::string adapter_descriptor_identity;
    std::string provider_key{"local-placeholder-durable-store"};
    std::string adapter_name{"AHFL Local Placeholder Durable Store Adapter"};
    std::string adapter_version{"0.28.0"};
    std::string interface_abi_version{std::string(kProviderSdkAdapterInterfaceAbiVersion)};
    std::string capability_descriptor_identity;
    ProviderSdkAdapterCapabilityDescriptor capability_descriptor;
    std::string error_taxonomy_version{std::string(kProviderSdkAdapterErrorTaxonomyVersion)};
    ProviderSdkAdapterNormalizedErrorKind normalized_error_kind{
        ProviderSdkAdapterNormalizedErrorKind::None};
    bool returns_placeholder_result{true};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    std::optional<std::string> provider_endpoint_uri;
    std::optional<std::string> credential_reference;
    std::optional<std::string> sdk_request_payload;
    std::optional<std::string> sdk_response_payload;
    std::optional<ProviderSdkAdapterInterfaceFailureAttribution> failure_attribution;
};

struct ProviderSdkAdapterInterfaceReview {
    std::string format_version{std::string(kProviderSdkAdapterInterfaceReviewFormatVersion)};
    std::string source_durable_store_import_provider_sdk_adapter_interface_plan_format_version{
        std::string(kProviderSdkAdapterInterfacePlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_adapter_request_identity;
    std::string durable_store_import_provider_sdk_adapter_interface_identity;
    ProviderSdkAdapterInterfaceStatus interface_status{ProviderSdkAdapterInterfaceStatus::Blocked};
    ProviderSdkAdapterInterfaceOperationKind operation_kind{
        ProviderSdkAdapterInterfaceOperationKind::NoopSdkAdapterRequestNotReady};
    std::string provider_registry_identity;
    std::string adapter_descriptor_identity;
    std::string capability_descriptor_identity;
    ProviderSdkAdapterCapabilitySupportStatus capability_support_status{
        ProviderSdkAdapterCapabilitySupportStatus::Unsupported};
    std::string error_taxonomy_version{std::string(kProviderSdkAdapterErrorTaxonomyVersion)};
    ProviderSdkAdapterNormalizedErrorKind normalized_error_kind{
        ProviderSdkAdapterNormalizedErrorKind::None};
    bool returns_placeholder_result{true};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderSdkAdapterInterfaceFailureAttribution> failure_attribution;
    std::string interface_boundary_summary;
    std::string next_step_recommendation;
    ProviderSdkAdapterInterfaceReviewNextActionKind next_action{
        ProviderSdkAdapterInterfaceReviewNextActionKind::ManualReviewRequired};
};

struct ProviderSdkAdapterInterfacePlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterInterfaceReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterInterfacePlanResult {
    std::optional<ProviderSdkAdapterInterfacePlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkAdapterInterfaceReviewResult {
    std::optional<ProviderSdkAdapterInterfaceReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSdkAdapterInterfacePlanValidationResult
validate_provider_sdk_adapter_interface_plan(const ProviderSdkAdapterInterfacePlan &plan);

[[nodiscard]] ProviderSdkAdapterInterfaceReviewValidationResult
validate_provider_sdk_adapter_interface_review(const ProviderSdkAdapterInterfaceReview &review);

[[nodiscard]] ProviderSdkAdapterInterfacePlanResult
build_provider_sdk_adapter_interface_plan(const ProviderSdkAdapterRequestPlan &request_plan);

[[nodiscard]] ProviderSdkAdapterInterfaceReviewResult
build_provider_sdk_adapter_interface_review(const ProviderSdkAdapterInterfacePlan &plan);

} // namespace ahfl::durable_store_import

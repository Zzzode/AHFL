#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_local_host_harness.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderSdkPayloadMaterializationPlanFormatVersion =
    "ahfl.durable-store-import-provider-sdk-payload-materialization-plan.v1";

inline constexpr std::string_view kProviderSdkPayloadAuditSummaryFormatVersion =
    "ahfl.durable-store-import-provider-sdk-payload-audit-summary.v1";

inline constexpr std::string_view kProviderFakeSdkPayloadSchemaVersion =
    "ahfl.fake-provider-sdk-payload-schema.v1";

enum class ProviderSdkPayloadOperationKind {
    PlanFakeProviderPayloadMaterialization,
    NoopHarnessNotReady,
};

enum class ProviderSdkPayloadStatus {
    Ready,
    Blocked,
};

enum class ProviderSdkPayloadFailureKind {
    HarnessNotReady,
    ForbiddenPayloadMaterial,
    UnsupportedProviderSchema,
};

enum class ProviderSdkPayloadNextActionKind {
    ReadyForMockAdapter,
    WaitForLocalHarness,
    ManualReviewRequired,
};

struct ProviderSdkPayloadFailureAttribution {
    ProviderSdkPayloadFailureKind kind{ProviderSdkPayloadFailureKind::HarnessNotReady};
    std::string message;
};

struct ProviderSdkPayloadRedactionSummary {
    bool secret_free{true};
    bool credential_free{true};
    bool token_free{true};
    int redacted_field_count{0};
    std::string summary{
        "fake provider payload contains only stable identities and digest metadata"};
};

struct ProviderSdkPayloadMaterializationPlan {
    std::string format_version{std::string(kProviderSdkPayloadMaterializationPlanFormatVersion)};
    std::string source_durable_store_import_provider_local_host_harness_review_format_version{
        std::string(kProviderLocalHostHarnessReviewFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_local_host_harness_record_identity;
    ProviderLocalHostHarnessNextActionKind source_harness_next_action{
        ProviderLocalHostHarnessNextActionKind::ManualReviewRequired};
    std::string durable_store_import_provider_sdk_payload_plan_identity;
    ProviderSdkPayloadOperationKind operation_kind{
        ProviderSdkPayloadOperationKind::NoopHarnessNotReady};
    ProviderSdkPayloadStatus payload_status{ProviderSdkPayloadStatus::Blocked};
    bool fake_provider_only{true};
    std::string provider_request_payload_schema_ref{
        std::string(kProviderFakeSdkPayloadSchemaVersion)};
    std::string payload_digest;
    ProviderSdkPayloadRedactionSummary redaction_summary;
    bool materializes_sdk_request_payload{true};
    bool persists_materialized_payload{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool materializes_secret_value{false};
    bool materializes_credential_material{false};
    bool materializes_token_value{false};
    bool invokes_provider_sdk{false};
    std::optional<std::string> raw_payload;
    std::optional<std::string> secret_value;
    std::optional<std::string> credential_material;
    std::optional<std::string> token_value;
    std::optional<ProviderSdkPayloadFailureAttribution> failure_attribution;
};

struct ProviderSdkPayloadAuditSummary {
    std::string format_version{std::string(kProviderSdkPayloadAuditSummaryFormatVersion)};
    std::string
        source_durable_store_import_provider_sdk_payload_materialization_plan_format_version{
            std::string(kProviderSdkPayloadMaterializationPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_payload_plan_identity;
    ProviderSdkPayloadStatus payload_status{ProviderSdkPayloadStatus::Blocked};
    std::string provider_request_payload_schema_ref{
        std::string(kProviderFakeSdkPayloadSchemaVersion)};
    std::string payload_digest;
    ProviderSdkPayloadRedactionSummary redaction_summary;
    bool fake_provider_only{true};
    bool raw_payload_persisted{false};
    std::string audit_summary;
    std::string next_step_recommendation;
    ProviderSdkPayloadNextActionKind next_action{
        ProviderSdkPayloadNextActionKind::ManualReviewRequired};
    std::optional<ProviderSdkPayloadFailureAttribution> failure_attribution;
};

struct ProviderSdkPayloadPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkPayloadAuditValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkPayloadPlanResult {
    std::optional<ProviderSdkPayloadMaterializationPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSdkPayloadAuditResult {
    std::optional<ProviderSdkPayloadAuditSummary> audit;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderSdkPayloadPlanValidationResult
validate_provider_sdk_payload_materialization_plan(
    const ProviderSdkPayloadMaterializationPlan &plan);

[[nodiscard]] ProviderSdkPayloadAuditValidationResult
validate_provider_sdk_payload_audit_summary(const ProviderSdkPayloadAuditSummary &audit);

[[nodiscard]] ProviderSdkPayloadPlanResult
build_provider_sdk_payload_materialization_plan(const ProviderLocalHostHarnessReview &review);

[[nodiscard]] ProviderSdkPayloadAuditResult
build_provider_sdk_payload_audit_summary(const ProviderSdkPayloadMaterializationPlan &plan);

} // namespace ahfl::durable_store_import

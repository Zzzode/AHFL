#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_host_execution.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderLocalHostExecutionReceiptFormatVersion =
    "ahfl.durable-store-import-provider-local-host-execution-receipt.v1";

inline constexpr std::string_view kProviderLocalHostExecutionReceiptReviewFormatVersion =
    "ahfl.durable-store-import-provider-local-host-execution-receipt-review.v1";

enum class ProviderLocalHostExecutionOperationKind {
    SimulateProviderLocalHostExecutionReceipt,
    NoopHostExecutionNotReady,
};

enum class ProviderLocalHostExecutionStatus {
    SimulatedReady,
    Blocked,
};

enum class ProviderLocalHostExecutionFailureKind {
    HostExecutionNotReady,
};

enum class ProviderLocalHostExecutionReceiptReviewNextActionKind {
    ReadyForProviderSdkAdapterPrototype,
    WaitForHostExecutionPlan,
    ManualReviewRequired,
};

struct ProviderLocalHostExecutionFailureAttribution {
    ProviderLocalHostExecutionFailureKind kind{
        ProviderLocalHostExecutionFailureKind::HostExecutionNotReady};
    std::string message;
};

struct ProviderLocalHostExecutionReceipt {
    std::string format_version{std::string(kProviderLocalHostExecutionReceiptFormatVersion)};
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
    ProviderHostExecutionStatus source_host_execution_status{ProviderHostExecutionStatus::Blocked};
    std::optional<std::string> source_provider_host_execution_descriptor_identity;
    std::optional<std::string> source_provider_host_receipt_placeholder_identity;
    std::string durable_store_import_provider_local_host_execution_receipt_identity;
    ProviderLocalHostExecutionOperationKind operation_kind{
        ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady};
    ProviderLocalHostExecutionStatus execution_status{ProviderLocalHostExecutionStatus::Blocked};
    std::optional<std::string> provider_local_host_execution_receipt_identity;
    bool starts_host_process{false};
    bool reads_host_environment{false};
    bool opens_network_connection{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderLocalHostExecutionFailureAttribution> failure_attribution;
};

struct ProviderLocalHostExecutionReceiptReview {
    std::string format_version{std::string(kProviderLocalHostExecutionReceiptReviewFormatVersion)};
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
    ProviderLocalHostExecutionStatus execution_status{ProviderLocalHostExecutionStatus::Blocked};
    ProviderLocalHostExecutionOperationKind operation_kind{
        ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady};
    std::optional<std::string> provider_local_host_execution_receipt_identity;
    bool starts_host_process{false};
    bool reads_host_environment{false};
    bool opens_network_connection{false};
    bool materializes_sdk_request_payload{false};
    bool invokes_provider_sdk{false};
    bool writes_host_filesystem{false};
    std::optional<ProviderLocalHostExecutionFailureAttribution> failure_attribution;
    std::string local_host_execution_boundary_summary;
    std::string next_step_recommendation;
    ProviderLocalHostExecutionReceiptReviewNextActionKind next_action{
        ProviderLocalHostExecutionReceiptReviewNextActionKind::ManualReviewRequired};
};

struct ProviderLocalHostExecutionReceiptValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostExecutionReceiptReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostExecutionReceiptResult {
    std::optional<ProviderLocalHostExecutionReceipt> receipt;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostExecutionReceiptReviewResult {
    std::optional<ProviderLocalHostExecutionReceiptReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderLocalHostExecutionReceiptValidationResult
validate_provider_local_host_execution_receipt(const ProviderLocalHostExecutionReceipt &receipt);

[[nodiscard]] ProviderLocalHostExecutionReceiptReviewValidationResult
validate_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceiptReview &review);

[[nodiscard]] ProviderLocalHostExecutionReceiptResult
build_provider_local_host_execution_receipt(const ProviderHostExecutionPlan &plan);

[[nodiscard]] ProviderLocalHostExecutionReceiptReviewResult
build_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceipt &receipt);

} // namespace ahfl::durable_store_import

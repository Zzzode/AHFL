#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_secret.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderLocalHostHarnessRequestFormatVersion =
    "ahfl.durable-store-import-provider-local-host-harness-request.v1";

inline constexpr std::string_view kProviderLocalHostHarnessExecutionRecordFormatVersion =
    "ahfl.durable-store-import-provider-local-host-harness-execution-record.v1";

inline constexpr std::string_view kProviderLocalHostHarnessReviewFormatVersion =
    "ahfl.durable-store-import-provider-local-host-harness-review.v1";

enum class ProviderLocalHostHarnessOperationKind {
    PlanHarnessRequest,
    RunTestHarness,
    NoopSecretPolicyNotReady,
    NoopHarnessRequestNotReady,
};

enum class ProviderLocalHostHarnessStatus {
    Ready,
    Blocked,
};

enum class ProviderLocalHostHarnessFixtureKind {
    Success,
    NonzeroExit,
    Timeout,
    SandboxDenied,
};

enum class ProviderLocalHostHarnessOutcomeKind {
    Succeeded,
    Failed,
    TimedOut,
    SandboxDenied,
    Blocked,
};

enum class ProviderLocalHostHarnessFailureKind {
    SecretPolicyNotReady,
    HarnessRequestNotReady,
    NonzeroExit,
    Timeout,
    SandboxDenied,
};

enum class ProviderLocalHostHarnessNextActionKind {
    ReadyForSdkPayloadMaterialization,
    WaitForSecretPolicy,
    ManualReviewRequired,
};

struct ProviderLocalHostHarnessFailureAttribution {
    ProviderLocalHostHarnessFailureKind kind{
        ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady};
    std::string message;
};

struct ProviderLocalHostHarnessSandboxPolicy {
    bool test_only{true};
    bool allow_network{false};
    bool allow_secret{false};
    bool allow_filesystem_write{false};
    bool allow_host_environment{false};
};

struct ProviderLocalHostHarnessExecutionRequest {
    std::string format_version{std::string(kProviderLocalHostHarnessRequestFormatVersion)};
    std::string source_durable_store_import_provider_secret_policy_review_format_version{
        std::string(kProviderSecretPolicyReviewFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_secret_resolver_response_identity;
    ProviderSecretPolicyNextActionKind source_secret_policy_next_action{
        ProviderSecretPolicyNextActionKind::ManualReviewRequired};
    std::string durable_store_import_provider_local_host_harness_request_identity;
    ProviderLocalHostHarnessOperationKind operation_kind{
        ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady};
    ProviderLocalHostHarnessStatus request_status{ProviderLocalHostHarnessStatus::Blocked};
    ProviderSecretHandleReference secret_handle;
    ProviderLocalHostHarnessSandboxPolicy sandbox_policy;
    int timeout_milliseconds{1000};
    ProviderLocalHostHarnessFixtureKind fixture_kind{ProviderLocalHostHarnessFixtureKind::Success};
    bool reads_secret_material{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool injects_secret{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderLocalHostHarnessFailureAttribution> failure_attribution;
};

struct ProviderLocalHostHarnessExecutionRecord {
    std::string format_version{std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion)};
    std::string source_durable_store_import_provider_local_host_harness_request_format_version{
        std::string(kProviderLocalHostHarnessRequestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_local_host_harness_request_identity;
    ProviderLocalHostHarnessStatus source_harness_request_status{
        ProviderLocalHostHarnessStatus::Blocked};
    std::string durable_store_import_provider_local_host_harness_record_identity;
    ProviderLocalHostHarnessOperationKind operation_kind{
        ProviderLocalHostHarnessOperationKind::NoopHarnessRequestNotReady};
    ProviderLocalHostHarnessStatus record_status{ProviderLocalHostHarnessStatus::Blocked};
    ProviderLocalHostHarnessOutcomeKind outcome_kind{ProviderLocalHostHarnessOutcomeKind::Blocked};
    int exit_code{-1};
    bool timed_out{false};
    bool sandbox_denied{false};
    std::string captured_diagnostic_summary;
    std::optional<std::string> captured_stdout_excerpt;
    std::optional<std::string> captured_stderr_excerpt;
    ProviderLocalHostHarnessSandboxPolicy sandbox_policy;
    bool reads_secret_material{false};
    bool opens_network_connection{false};
    bool reads_host_environment{false};
    bool writes_host_filesystem{false};
    bool injects_secret{false};
    bool invokes_provider_sdk{false};
    std::optional<ProviderLocalHostHarnessFailureAttribution> failure_attribution;
};

struct ProviderLocalHostHarnessReview {
    std::string format_version{std::string(kProviderLocalHostHarnessReviewFormatVersion)};
    std::string
        source_durable_store_import_provider_local_host_harness_execution_record_format_version{
            std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_local_host_harness_record_identity;
    ProviderLocalHostHarnessStatus record_status{ProviderLocalHostHarnessStatus::Blocked};
    ProviderLocalHostHarnessOutcomeKind outcome_kind{ProviderLocalHostHarnessOutcomeKind::Blocked};
    ProviderLocalHostHarnessSandboxPolicy sandbox_policy;
    std::string harness_boundary_summary;
    std::string next_step_recommendation;
    ProviderLocalHostHarnessNextActionKind next_action{
        ProviderLocalHostHarnessNextActionKind::ManualReviewRequired};
    std::optional<ProviderLocalHostHarnessFailureAttribution> failure_attribution;
};

struct ProviderLocalHostHarnessRequestValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostHarnessRecordValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostHarnessReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostHarnessRequestResult {
    std::optional<ProviderLocalHostHarnessExecutionRequest> request;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostHarnessRecordResult {
    std::optional<ProviderLocalHostHarnessExecutionRecord> record;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalHostHarnessReviewResult {
    std::optional<ProviderLocalHostHarnessReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderLocalHostHarnessRequestValidationResult
validate_provider_local_host_harness_execution_request(
    const ProviderLocalHostHarnessExecutionRequest &request);

[[nodiscard]] ProviderLocalHostHarnessRecordValidationResult
validate_provider_local_host_harness_execution_record(
    const ProviderLocalHostHarnessExecutionRecord &record);

[[nodiscard]] ProviderLocalHostHarnessReviewValidationResult
validate_provider_local_host_harness_review(const ProviderLocalHostHarnessReview &review);

[[nodiscard]] ProviderLocalHostHarnessRequestResult
build_provider_local_host_harness_execution_request(
    const ProviderSecretPolicyReview &secret_policy_review);

[[nodiscard]] ProviderLocalHostHarnessRecordResult
run_provider_local_host_test_harness(const ProviderLocalHostHarnessExecutionRequest &request);

[[nodiscard]] ProviderLocalHostHarnessReviewResult
build_provider_local_host_harness_review(const ProviderLocalHostHarnessExecutionRecord &record);

} // namespace ahfl::durable_store_import

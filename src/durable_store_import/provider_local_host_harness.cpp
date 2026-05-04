#include "ahfl/durable_store_import/provider_local_host_harness.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_HOST_HARNESS";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderLocalHostHarnessStatus status) {
    return status == ProviderLocalHostHarnessStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string request_identity(const ProviderSecretPolicyReview &review,
                                           ProviderLocalHostHarnessStatus status) {
    return "durable-store-import-provider-local-host-harness-request::" + review.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string record_identity(const ProviderLocalHostHarnessExecutionRequest &request,
                                          ProviderLocalHostHarnessOutcomeKind outcome) {
    std::string outcome_slug = "blocked";
    switch (outcome) {
    case ProviderLocalHostHarnessOutcomeKind::Succeeded:
        outcome_slug = "succeeded";
        break;
    case ProviderLocalHostHarnessOutcomeKind::Failed:
        outcome_slug = "failed";
        break;
    case ProviderLocalHostHarnessOutcomeKind::TimedOut:
        outcome_slug = "timeout";
        break;
    case ProviderLocalHostHarnessOutcomeKind::SandboxDenied:
        outcome_slug = "sandbox-denied";
        break;
    case ProviderLocalHostHarnessOutcomeKind::Blocked:
        outcome_slug = "blocked";
        break;
    }
    return "durable-store-import-provider-local-host-harness-record::" + request.session_id +
           "::" + outcome_slug;
}

[[nodiscard]] ProviderLocalHostHarnessFailureAttribution
secret_policy_not_ready_failure(const ProviderSecretPolicyReview &review) {
    std::string message =
        "local host harness request cannot proceed because secret policy is not ready";
    if (review.failure_attribution.has_value()) {
        message = review.failure_attribution->message;
    }
    return ProviderLocalHostHarnessFailureAttribution{
        .kind = ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderLocalHostHarnessFailureAttribution
request_not_ready_failure(const ProviderLocalHostHarnessExecutionRequest &request) {
    std::string message =
        "local host harness execution record cannot proceed because harness request is not ready";
    if (request.failure_attribution.has_value()) {
        message = request.failure_attribution->message;
    }
    return ProviderLocalHostHarnessFailureAttribution{
        .kind = ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderLocalHostHarnessFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_sandbox_policy(const ProviderLocalHostHarnessSandboxPolicy &policy,
                             DiagnosticBag &diagnostics,
                             std::string_view owner) {
    if (!policy.test_only || policy.allow_network || policy.allow_secret ||
        policy.allow_filesystem_write || policy.allow_host_environment) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " sandbox policy must be test-only with no network, secret, "
                                  "filesystem write, or host environment");
    }
}

void validate_no_forbidden_host_access(bool reads_secret_material,
                                       bool opens_network_connection,
                                       bool reads_host_environment,
                                       bool writes_host_filesystem,
                                       bool injects_secret,
                                       bool invokes_provider_sdk,
                                       DiagnosticBag &diagnostics,
                                       std::string_view owner) {
    if (reads_secret_material || injects_secret) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read or inject secret");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_host_environment) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot write host filesystem");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
}

[[nodiscard]] ProviderLocalHostHarnessNextActionKind
next_action_for_record(const ProviderLocalHostHarnessExecutionRecord &record) {
    if (record.record_status == ProviderLocalHostHarnessStatus::Ready &&
        record.outcome_kind == ProviderLocalHostHarnessOutcomeKind::Succeeded) {
        return ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization;
    }
    if (record.failure_attribution.has_value() &&
        record.failure_attribution->kind ==
            ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady) {
        return ProviderLocalHostHarnessNextActionKind::WaitForSecretPolicy;
    }
    return ProviderLocalHostHarnessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderLocalHostHarnessExecutionRecord &record) {
    if (record.outcome_kind == ProviderLocalHostHarnessOutcomeKind::Succeeded) {
        return "test-only local host harness completed without network, secret, host environment, "
               "filesystem write, or provider SDK access";
    }
    return "test-only local host harness did not produce a successful controlled execution record";
}

} // namespace

ProviderLocalHostHarnessRequestValidationResult
validate_provider_local_host_harness_execution_request(
    const ProviderLocalHostHarnessExecutionRequest &request) {
    ProviderLocalHostHarnessRequestValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (request.format_version != kProviderLocalHostHarnessRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider local host harness request format_version must be '" +
                                  std::string(kProviderLocalHostHarnessRequestFormatVersion) + "'");
    }
    if (request.source_durable_store_import_provider_secret_policy_review_format_version !=
        kProviderSecretPolicyReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local host harness request source format_version must be '" +
                std::string(kProviderSecretPolicyReviewFormatVersion) + "'");
    }
    if (request.workflow_canonical_name.empty() || request.session_id.empty() ||
        request.input_fixture.empty() ||
        request.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        request.durable_store_import_provider_local_host_harness_request_identity.empty()) {
        emit_validation_error(
            diagnostics, "provider local host harness request identity fields must not be empty");
    }
    if (request.timeout_milliseconds <= 0) {
        emit_validation_error(diagnostics,
                              "provider local host harness request timeout must be positive");
    }
    validate_sandbox_policy(
        request.sandbox_policy, diagnostics, "provider local host harness request");
    validate_failure(
        request.failure_attribution, diagnostics, "provider local host harness request");
    validate_no_forbidden_host_access(request.reads_secret_material,
                                      request.opens_network_connection,
                                      request.reads_host_environment,
                                      request.writes_host_filesystem,
                                      request.injects_secret,
                                      request.invokes_provider_sdk,
                                      diagnostics,
                                      "provider local host harness request");
    if (request.request_status == ProviderLocalHostHarnessStatus::Ready) {
        if (request.source_secret_policy_next_action !=
                ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness ||
            request.operation_kind != ProviderLocalHostHarnessOperationKind::PlanHarnessRequest ||
            request.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "provider local host harness request ready status requires ready "
                                  "secret policy and no failure");
        }
    } else if (request.operation_kind ==
                   ProviderLocalHostHarnessOperationKind::PlanHarnessRequest ||
               !request.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider local host harness request blocked status requires noop "
                              "operation and failure_attribution");
    }
    return result;
}

ProviderLocalHostHarnessRecordValidationResult
validate_provider_local_host_harness_execution_record(
    const ProviderLocalHostHarnessExecutionRecord &record) {
    ProviderLocalHostHarnessRecordValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (record.format_version != kProviderLocalHostHarnessExecutionRecordFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local host harness execution record format_version must be '" +
                std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion) + "'");
    }
    if (record.source_durable_store_import_provider_local_host_harness_request_format_version !=
        kProviderLocalHostHarnessRequestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local host harness execution record source format_version must be '" +
                std::string(kProviderLocalHostHarnessRequestFormatVersion) + "'");
    }
    if (record.workflow_canonical_name.empty() || record.session_id.empty() ||
        record.input_fixture.empty() ||
        record.durable_store_import_provider_local_host_harness_request_identity.empty() ||
        record.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        record.captured_diagnostic_summary.empty()) {
        emit_validation_error(diagnostics,
                              "provider local host harness execution record identity and "
                              "diagnostic fields must not be empty");
    }
    validate_sandbox_policy(
        record.sandbox_policy, diagnostics, "provider local host harness record");
    validate_failure(record.failure_attribution, diagnostics, "provider local host harness record");
    validate_no_forbidden_host_access(record.reads_secret_material,
                                      record.opens_network_connection,
                                      record.reads_host_environment,
                                      record.writes_host_filesystem,
                                      record.injects_secret,
                                      record.invokes_provider_sdk,
                                      diagnostics,
                                      "provider local host harness record");
    if (record.record_status == ProviderLocalHostHarnessStatus::Ready) {
        if (record.source_harness_request_status != ProviderLocalHostHarnessStatus::Ready ||
            record.operation_kind != ProviderLocalHostHarnessOperationKind::RunTestHarness) {
            emit_validation_error(diagnostics,
                                  "provider local host harness record ready status requires ready "
                                  "request and test harness run");
        }
    } else if (record.operation_kind == ProviderLocalHostHarnessOperationKind::RunTestHarness ||
               !record.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider local host harness record blocked status requires noop "
                              "operation and failure_attribution");
    }
    return result;
}

ProviderLocalHostHarnessReviewValidationResult
validate_provider_local_host_harness_review(const ProviderLocalHostHarnessReview &review) {
    ProviderLocalHostHarnessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderLocalHostHarnessReviewFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider local host harness review format_version must be '" +
                                  std::string(kProviderLocalHostHarnessReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_local_host_harness_execution_record_format_version !=
        kProviderLocalHostHarnessExecutionRecordFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local host harness review source format_version must be '" +
                std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        review.harness_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider local host harness review identity and summary fields must not be empty");
    }
    validate_sandbox_policy(
        review.sandbox_policy, diagnostics, "provider local host harness review");
    validate_failure(review.failure_attribution, diagnostics, "provider local host harness review");
    if (review.next_action ==
            ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization &&
        (review.record_status != ProviderLocalHostHarnessStatus::Ready ||
         review.outcome_kind != ProviderLocalHostHarnessOutcomeKind::Succeeded ||
         review.failure_attribution.has_value())) {
        emit_validation_error(diagnostics,
                              "provider local host harness review payload next action requires "
                              "successful record and no failure");
    }
    return result;
}

ProviderLocalHostHarnessRequestResult build_provider_local_host_harness_execution_request(
    const ProviderSecretPolicyReview &secret_policy_review) {
    ProviderLocalHostHarnessRequestResult result;
    result.diagnostics.append(
        validate_provider_secret_policy_review(secret_policy_review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = secret_policy_review.next_action ==
                       ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness;
    const auto status =
        ready ? ProviderLocalHostHarnessStatus::Ready : ProviderLocalHostHarnessStatus::Blocked;
    ProviderLocalHostHarnessExecutionRequest request;
    request.workflow_canonical_name = secret_policy_review.workflow_canonical_name;
    request.session_id = secret_policy_review.session_id;
    request.run_id = secret_policy_review.run_id;
    request.input_fixture = secret_policy_review.input_fixture;
    request.durable_store_import_provider_secret_resolver_response_identity =
        secret_policy_review.durable_store_import_provider_secret_resolver_response_identity;
    request.source_secret_policy_next_action = secret_policy_review.next_action;
    request.durable_store_import_provider_local_host_harness_request_identity =
        request_identity(secret_policy_review, status);
    request.request_status = status;
    request.secret_handle = secret_policy_review.secret_handle;
    if (ready) {
        request.operation_kind = ProviderLocalHostHarnessOperationKind::PlanHarnessRequest;
    } else {
        request.operation_kind = ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady;
        request.failure_attribution = secret_policy_not_ready_failure(secret_policy_review);
    }
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.request = std::move(request);
    return result;
}

ProviderLocalHostHarnessRecordResult
run_provider_local_host_test_harness(const ProviderLocalHostHarnessExecutionRequest &request) {
    ProviderLocalHostHarnessRecordResult result;
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalHostHarnessExecutionRecord record;
    record.workflow_canonical_name = request.workflow_canonical_name;
    record.session_id = request.session_id;
    record.run_id = request.run_id;
    record.input_fixture = request.input_fixture;
    record.durable_store_import_provider_local_host_harness_request_identity =
        request.durable_store_import_provider_local_host_harness_request_identity;
    record.source_harness_request_status = request.request_status;
    record.sandbox_policy = request.sandbox_policy;
    if (request.request_status != ProviderLocalHostHarnessStatus::Ready) {
        record.operation_kind = ProviderLocalHostHarnessOperationKind::NoopHarnessRequestNotReady;
        record.record_status = ProviderLocalHostHarnessStatus::Blocked;
        record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Blocked;
        record.exit_code = -1;
        record.captured_diagnostic_summary = "harness request was blocked before test runner";
        record.failure_attribution = request_not_ready_failure(request);
    } else {
        record.operation_kind = ProviderLocalHostHarnessOperationKind::RunTestHarness;
        record.record_status = ProviderLocalHostHarnessStatus::Ready;
        switch (request.fixture_kind) {
        case ProviderLocalHostHarnessFixtureKind::Success:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Succeeded;
            record.exit_code = 0;
            record.captured_diagnostic_summary = "test harness completed successfully";
            record.captured_stdout_excerpt = "ok";
            break;
        case ProviderLocalHostHarnessFixtureKind::NonzeroExit:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Failed;
            record.exit_code = 42;
            record.captured_diagnostic_summary = "test harness exited with nonzero status";
            record.captured_stderr_excerpt = "mock provider failed";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::NonzeroExit,
                .message = "test harness exited with nonzero status 42",
            };
            break;
        case ProviderLocalHostHarnessFixtureKind::Timeout:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::TimedOut;
            record.exit_code = -1;
            record.timed_out = true;
            record.captured_diagnostic_summary = "test harness timed out";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::Timeout,
                .message = "test harness exceeded timeout policy",
            };
            break;
        case ProviderLocalHostHarnessFixtureKind::SandboxDenied:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::SandboxDenied;
            record.exit_code = -1;
            record.sandbox_denied = true;
            record.captured_diagnostic_summary = "test harness was denied by sandbox policy";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::SandboxDenied,
                .message = "test harness requested a sandbox-forbidden operation",
            };
            break;
        }
    }
    record.durable_store_import_provider_local_host_harness_record_identity =
        record_identity(request, record.outcome_kind);
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_record(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.record = std::move(record);
    return result;
}

ProviderLocalHostHarnessReviewResult
build_provider_local_host_harness_review(const ProviderLocalHostHarnessExecutionRecord &record) {
    ProviderLocalHostHarnessReviewResult result;
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_record(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalHostHarnessReview review;
    review.workflow_canonical_name = record.workflow_canonical_name;
    review.session_id = record.session_id;
    review.run_id = record.run_id;
    review.input_fixture = record.input_fixture;
    review.durable_store_import_provider_local_host_harness_record_identity =
        record.durable_store_import_provider_local_host_harness_record_identity;
    review.record_status = record.record_status;
    review.outcome_kind = record.outcome_kind;
    review.sandbox_policy = record.sandbox_policy;
    review.harness_boundary_summary = boundary_summary(record);
    review.next_action = next_action_for_record(record);
    review.next_step_recommendation =
        review.next_action ==
                ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization
            ? "ready for fake SDK payload materialization"
            : "manual review or source readiness is required before payload materialization";
    review.failure_attribution = record.failure_attribution;
    result.diagnostics.append(validate_provider_local_host_harness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

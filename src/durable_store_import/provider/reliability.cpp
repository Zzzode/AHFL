// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_retry.cpp ----

#include "ahfl/durable_store_import/provider_retry.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_retry_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_WRITE_RETRY";

void provider_retry_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_retry_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_retry_detail_normalized_slug(ProviderSdkMockAdapterNormalizedResultKind result) {
    switch (result) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        return "accepted";
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
        return "provider-failure";
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
        return "timeout";
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        return "throttled";
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        return "conflict";
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        return "schema-mismatch";
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] ProviderWriteRetryFailureAttribution provider_retry_detail_failure_for_result(
    const ProviderSdkMockAdapterExecutionResult &adapter_result,
    ProviderWriteRetryFailureKind kind,
    std::string fallback) {
    if (adapter_result.failure_attribution.has_value()) {
        fallback = adapter_result.failure_attribution->message;
    }
    return ProviderWriteRetryFailureAttribution{
        .kind = kind,
        .message = std::move(fallback),
    };
}

void provider_retry_detail_validate_failure(
    const std::optional<ProviderWriteRetryFailureAttribution> &failure,
    DiagnosticBag &diagnostics) {
    if (failure.has_value() && failure->message.empty()) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision failure_attribution message must not be empty");
    }
}

} // namespace

ProviderWriteRetryDecisionValidationResult
validate_provider_write_retry_decision(const ProviderWriteRetryDecision &decision) {
    ProviderWriteRetryDecisionValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (decision.format_version != kProviderWriteRetryDecisionFormatVersion) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision format_version must be '" +
                std::string(kProviderWriteRetryDecisionFormatVersion) + "'");
    }
    if (decision
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision source format_version must be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (decision.workflow_canonical_name.empty() || decision.session_id.empty() ||
        decision.input_fixture.empty() ||
        decision.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        decision.durable_store_import_provider_write_retry_decision_identity.empty() ||
        decision.idempotency_key_namespace.empty() || decision.idempotency_key.empty() ||
        decision.retry_token_version != kProviderWriteRetryTokenVersion ||
        decision.retry_token_placeholder_identity.empty() ||
        decision.duplicate_detection_summary.empty() || decision.retry_decision_summary.empty()) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision identity and summary fields must not be empty");
    }
    provider_retry_detail_validate_failure(decision.failure_attribution, diagnostics);
    if (decision.retry_allowed &&
        decision.retry_eligibility != ProviderWriteRetryEligibility::Retryable) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision retry_allowed requires retryable eligibility");
    }
    if (decision.duplicate_write_possible &&
        decision.retry_eligibility != ProviderWriteRetryEligibility::DuplicateReviewRequired) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision duplicate flag requires duplicate review eligibility");
    }
    if (decision.retry_eligibility == ProviderWriteRetryEligibility::Blocked &&
        !decision.failure_attribution.has_value()) {
        provider_retry_detail_emit_validation_error(
            diagnostics,
            "provider write retry decision blocked status requires failure_attribution");
    }
    return result;
}

ProviderWriteRetryDecisionResult
build_provider_write_retry_decision(const ProviderSdkMockAdapterExecutionResult &adapter_result) {
    ProviderWriteRetryDecisionResult result;
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(adapter_result).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderWriteRetryDecision decision;
    decision.workflow_canonical_name = adapter_result.workflow_canonical_name;
    decision.session_id = adapter_result.session_id;
    decision.run_id = adapter_result.run_id;
    decision.input_fixture = adapter_result.input_fixture;
    decision.durable_store_import_provider_sdk_mock_adapter_result_identity =
        adapter_result.durable_store_import_provider_sdk_mock_adapter_result_identity;
    decision.source_normalized_result = adapter_result.normalized_result;
    decision.durable_store_import_provider_write_retry_decision_identity =
        "durable-store-import-provider-write-retry-decision::" + adapter_result.session_id +
        "::" + provider_retry_detail_normalized_slug(adapter_result.normalized_result);
    decision.idempotency_key = decision.idempotency_key_namespace +
                               "::" + adapter_result.session_id +
                               "::" + adapter_result.input_fixture;
    decision.retry_token_placeholder_identity =
        "provider-write-retry-token-placeholder::" + decision.idempotency_key;
    switch (adapter_result.normalized_result) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        decision.retry_eligibility = ProviderWriteRetryEligibility::NotApplicable;
        decision.duplicate_detection_summary = "accepted provider result has no retry requirement";
        decision.retry_decision_summary = "do not retry an accepted provider write";
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        decision.retry_eligibility = ProviderWriteRetryEligibility::Retryable;
        decision.retry_allowed = true;
        decision.duplicate_detection_summary =
            "retry must reuse idempotency key before provider write is attempted again";
        decision.retry_decision_summary = "retry is allowed with the retry token placeholder";
        decision.failure_attribution = provider_retry_detail_failure_for_result(
            adapter_result,
            ProviderWriteRetryFailureKind::RetryableProviderFailure,
            "provider failure is retryable");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        decision.retry_eligibility = ProviderWriteRetryEligibility::DuplicateReviewRequired;
        decision.duplicate_write_possible = true;
        decision.duplicate_detection_summary =
            "conflict result may indicate duplicate write and requires provider commit lookup";
        decision.retry_decision_summary =
            "do not retry until duplicate write review resolves the conflict";
        decision.failure_attribution = provider_retry_detail_failure_for_result(
            adapter_result,
            ProviderWriteRetryFailureKind::DuplicateWriteSuspected,
            "provider conflict may indicate duplicate write");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        decision.retry_eligibility = ProviderWriteRetryEligibility::NonRetryable;
        decision.duplicate_detection_summary = "no duplicate write signal was observed";
        decision.retry_decision_summary =
            "provider failure is not retryable without manual remediation";
        decision.failure_attribution = provider_retry_detail_failure_for_result(
            adapter_result,
            ProviderWriteRetryFailureKind::NonRetryableProviderFailure,
            "provider failure is not retryable");
        break;
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        decision.retry_eligibility = ProviderWriteRetryEligibility::Blocked;
        decision.duplicate_detection_summary =
            "adapter result was blocked before retry classification";
        decision.retry_decision_summary = "wait for adapter result before retry classification";
        decision.failure_attribution = provider_retry_detail_failure_for_result(
            adapter_result,
            ProviderWriteRetryFailureKind::AdapterResultNotReady,
            "adapter result is not ready");
        break;
    }
    result.diagnostics.append(validate_provider_write_retry_decision(decision).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.decision = std::move(decision);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_commit.cpp ----

#include "ahfl/durable_store_import/provider_commit.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_commit_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_WRITE_COMMIT";

void provider_commit_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_commit_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_commit_detail_commit_slug(ProviderWriteCommitStatus status) {
    switch (status) {
    case ProviderWriteCommitStatus::Committed:
        return "committed";
    case ProviderWriteCommitStatus::Duplicate:
        return "duplicate";
    case ProviderWriteCommitStatus::Partial:
        return "partial";
    case ProviderWriteCommitStatus::Failed:
        return "failed";
    case ProviderWriteCommitStatus::Blocked:
        return "blocked";
    }
    return "unknown";
}

void provider_commit_detail_validate_failure(
    const std::optional<ProviderWriteCommitFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_commit_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

[[nodiscard]] ProviderWriteCommitFailureAttribution
provider_commit_detail_commit_failure(ProviderWriteCommitFailureKind kind, std::string message) {
    return ProviderWriteCommitFailureAttribution{
        .kind = kind,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderWriteCommitNextActionKind
provider_commit_detail_next_action_for_receipt(const ProviderWriteCommitReceipt &receipt) {
    switch (receipt.commit_status) {
    case ProviderWriteCommitStatus::Committed:
    case ProviderWriteCommitStatus::Duplicate:
        return ProviderWriteCommitNextActionKind::ReadyForRecoveryAudit;
    case ProviderWriteCommitStatus::Partial:
        return ProviderWriteCommitNextActionKind::RetryUsingIdempotencyContract;
    case ProviderWriteCommitStatus::Failed:
    case ProviderWriteCommitStatus::Blocked:
        return ProviderWriteCommitNextActionKind::ManualReviewRequired;
    }
    return ProviderWriteCommitNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderWriteCommitReceiptValidationResult
validate_provider_write_commit_receipt(const ProviderWriteCommitReceipt &receipt) {
    ProviderWriteCommitReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (receipt.format_version != kProviderWriteCommitReceiptFormatVersion) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit receipt format_version must be '" +
                std::string(kProviderWriteCommitReceiptFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_write_retry_decision_format_version !=
        kProviderWriteRetryDecisionFormatVersion) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit receipt source format_version must be '" +
                std::string(kProviderWriteRetryDecisionFormatVersion) + "'");
    }
    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty() ||
        receipt.input_fixture.empty() ||
        receipt.durable_store_import_provider_write_retry_decision_identity.empty() ||
        receipt.durable_store_import_provider_write_commit_receipt_identity.empty() ||
        receipt.provider_commit_reference.empty() || receipt.provider_commit_digest.empty() ||
        receipt.idempotency_key.empty()) {
        provider_commit_detail_emit_validation_error(
            diagnostics, "provider write commit receipt identity fields must not be empty");
    }
    if (!receipt.secret_free || receipt.raw_provider_payload_persisted) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit receipt must be secret-free and cannot "
            "persist raw provider payload");
    }
    provider_commit_detail_validate_failure(
        receipt.failure_attribution, diagnostics, "provider write commit receipt");
    if (receipt.commit_status != ProviderWriteCommitStatus::Committed &&
        !receipt.failure_attribution.has_value()) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit receipt non-committed status requires failure_attribution");
    }
    return result;
}

ProviderWriteCommitReviewValidationResult
validate_provider_write_commit_review(const ProviderWriteCommitReview &review) {
    ProviderWriteCommitReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderWriteCommitReviewFormatVersion) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit review format_version must be '" +
                std::string(kProviderWriteCommitReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_commit_receipt_format_version !=
        kProviderWriteCommitReceiptFormatVersion) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit review source format_version must be '" +
                std::string(kProviderWriteCommitReceiptFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_commit_receipt_identity.empty() ||
        review.provider_commit_reference.empty() || review.provider_commit_digest.empty() ||
        review.commit_review_summary.empty() || review.next_step_recommendation.empty()) {
        provider_commit_detail_emit_validation_error(
            diagnostics,
            "provider write commit review identity and summary fields must not be empty");
    }
    provider_commit_detail_validate_failure(
        review.failure_attribution, diagnostics, "provider write commit review");
    return result;
}

ProviderWriteCommitReceiptResult
build_provider_write_commit_receipt(const ProviderWriteRetryDecision &decision) {
    ProviderWriteCommitReceiptResult result;
    result.diagnostics.append(validate_provider_write_retry_decision(decision).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderWriteCommitReceipt receipt;
    receipt.workflow_canonical_name = decision.workflow_canonical_name;
    receipt.session_id = decision.session_id;
    receipt.run_id = decision.run_id;
    receipt.input_fixture = decision.input_fixture;
    receipt.durable_store_import_provider_write_retry_decision_identity =
        decision.durable_store_import_provider_write_retry_decision_identity;
    receipt.idempotency_key = decision.idempotency_key;
    switch (decision.retry_eligibility) {
    case ProviderWriteRetryEligibility::NotApplicable:
        receipt.commit_status = ProviderWriteCommitStatus::Committed;
        receipt.provider_commit_reference =
            "provider-commit::" + decision.session_id + "::accepted";
        receipt.provider_commit_digest = "sha256:provider-commit:" + decision.session_id;
        break;
    case ProviderWriteRetryEligibility::DuplicateReviewRequired:
        receipt.commit_status = ProviderWriteCommitStatus::Duplicate;
        receipt.provider_commit_reference =
            "provider-commit::" + decision.session_id + "::duplicate-review";
        receipt.provider_commit_digest = "sha256:provider-commit-duplicate:" + decision.session_id;
        receipt.failure_attribution = provider_commit_detail_commit_failure(
            ProviderWriteCommitFailureKind::DuplicateDetected,
            "provider conflict mapped to duplicate commit review");
        break;
    case ProviderWriteRetryEligibility::Retryable:
        receipt.commit_status = ProviderWriteCommitStatus::Partial;
        receipt.provider_commit_reference =
            "provider-commit::" + decision.session_id + "::partial-retryable";
        receipt.provider_commit_digest = "sha256:provider-commit-partial:" + decision.session_id;
        receipt.failure_attribution = provider_commit_detail_commit_failure(
            ProviderWriteCommitFailureKind::PartialCommit,
            "provider write requires retry under idempotency contract");
        break;
    case ProviderWriteRetryEligibility::NonRetryable:
        receipt.commit_status = ProviderWriteCommitStatus::Failed;
        receipt.provider_commit_reference = "provider-commit::" + decision.session_id + "::failed";
        receipt.provider_commit_digest = "sha256:provider-commit-failed:" + decision.session_id;
        receipt.failure_attribution = provider_commit_detail_commit_failure(
            ProviderWriteCommitFailureKind::ProviderFailure,
            "provider failure cannot be committed without manual remediation");
        break;
    case ProviderWriteRetryEligibility::Blocked:
        receipt.commit_status = ProviderWriteCommitStatus::Blocked;
        receipt.provider_commit_reference = "provider-commit::" + decision.session_id + "::blocked";
        receipt.provider_commit_digest = "sha256:provider-commit-blocked:" + decision.session_id;
        receipt.failure_attribution = provider_commit_detail_commit_failure(
            ProviderWriteCommitFailureKind::RetryDecisionNotReady, "retry decision is blocked");
        break;
    }
    receipt.durable_store_import_provider_write_commit_receipt_identity =
        "durable-store-import-provider-write-commit-receipt::" + decision.session_id +
        "::" + provider_commit_detail_commit_slug(receipt.commit_status);
    result.diagnostics.append(validate_provider_write_commit_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.receipt = std::move(receipt);
    return result;
}

ProviderWriteCommitReviewResult
build_provider_write_commit_review(const ProviderWriteCommitReceipt &receipt) {
    ProviderWriteCommitReviewResult result;
    result.diagnostics.append(validate_provider_write_commit_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderWriteCommitReview review;
    review.workflow_canonical_name = receipt.workflow_canonical_name;
    review.session_id = receipt.session_id;
    review.run_id = receipt.run_id;
    review.input_fixture = receipt.input_fixture;
    review.durable_store_import_provider_write_commit_receipt_identity =
        receipt.durable_store_import_provider_write_commit_receipt_identity;
    review.commit_status = receipt.commit_status;
    review.provider_commit_reference = receipt.provider_commit_reference;
    review.provider_commit_digest = receipt.provider_commit_digest;
    review.commit_review_summary = "provider write commit receipt status is " +
                                   provider_commit_detail_commit_slug(receipt.commit_status);
    review.next_action = provider_commit_detail_next_action_for_receipt(receipt);
    review.next_step_recommendation =
        review.next_action == ProviderWriteCommitNextActionKind::ReadyForRecoveryAudit
            ? "ready for durable recovery audit"
            : "manual review or idempotent retry is required";
    review.failure_attribution = receipt.failure_attribution;
    result.diagnostics.append(validate_provider_write_commit_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_recovery.cpp ----

#include "ahfl/durable_store_import/provider_recovery.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_recovery_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_WRITE_RECOVERY";

void provider_recovery_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                    std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_recovery_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_recovery_detail_checkpoint_slug(ProviderWriteRecoveryCheckpointStatus status) {
    switch (status) {
    case ProviderWriteRecoveryCheckpointStatus::StableCommitted:
        return "stable-committed";
    case ProviderWriteRecoveryCheckpointStatus::DuplicateReview:
        return "duplicate-review";
    case ProviderWriteRecoveryCheckpointStatus::PartialWrite:
        return "partial-write";
    case ProviderWriteRecoveryCheckpointStatus::FailedWrite:
        return "failed-write";
    case ProviderWriteRecoveryCheckpointStatus::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] std::string
provider_recovery_detail_plan_action_slug(ProviderWriteRecoveryPlanAction action) {
    switch (action) {
    case ProviderWriteRecoveryPlanAction::NoopCommitted:
        return "noop-committed";
    case ProviderWriteRecoveryPlanAction::LookupDuplicateCommit:
        return "lookup-duplicate-commit";
    case ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey:
        return "resume-with-idempotency-key";
    case ProviderWriteRecoveryPlanAction::ManualRemediation:
        return "manual-remediation";
    case ProviderWriteRecoveryPlanAction::WaitForCommitReceipt:
        return "wait-for-commit-receipt";
    }
    return "unknown";
}

[[nodiscard]] ProviderWriteRecoveryFailureAttribution
provider_recovery_detail_recovery_failure(ProviderWriteRecoveryFailureKind kind,
                                          std::string message) {
    return ProviderWriteRecoveryFailureAttribution{
        .kind = kind,
        .message = std::move(message),
    };
}

void provider_recovery_detail_validate_failure(
    const std::optional<ProviderWriteRecoveryFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

[[nodiscard]] ProviderWriteRecoveryNextActionKind
provider_recovery_detail_next_action_for_plan(ProviderWriteRecoveryPlanAction action) {
    switch (action) {
    case ProviderWriteRecoveryPlanAction::NoopCommitted:
        return ProviderWriteRecoveryNextActionKind::ReadyForAuditEvent;
    case ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey:
        return ProviderWriteRecoveryNextActionKind::ResumeUsingToken;
    case ProviderWriteRecoveryPlanAction::LookupDuplicateCommit:
    case ProviderWriteRecoveryPlanAction::ManualRemediation:
    case ProviderWriteRecoveryPlanAction::WaitForCommitReceipt:
        return ProviderWriteRecoveryNextActionKind::ManualReviewRequired;
    }
    return ProviderWriteRecoveryNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderWriteRecoveryCheckpointValidationResult
validate_provider_write_recovery_checkpoint(const ProviderWriteRecoveryCheckpoint &checkpoint) {
    ProviderWriteRecoveryCheckpointValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (checkpoint.format_version != kProviderWriteRecoveryCheckpointFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery checkpoint format_version must be '" +
                std::string(kProviderWriteRecoveryCheckpointFormatVersion) + "'");
    }
    if (checkpoint.source_durable_store_import_provider_write_commit_receipt_format_version !=
        kProviderWriteCommitReceiptFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery checkpoint source format_version must be '" +
                std::string(kProviderWriteCommitReceiptFormatVersion) + "'");
    }
    if (checkpoint.workflow_canonical_name.empty() || checkpoint.session_id.empty() ||
        checkpoint.input_fixture.empty() ||
        checkpoint.durable_store_import_provider_write_commit_receipt_identity.empty() ||
        checkpoint.durable_store_import_provider_write_recovery_checkpoint_identity.empty() ||
        checkpoint.recovery_checkpoint_reference.empty() ||
        checkpoint.resume_token_version != kProviderWriteResumeTokenVersion ||
        checkpoint.resume_token_placeholder_identity.empty() ||
        checkpoint.idempotency_key.empty() || checkpoint.recovery_summary.empty()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery checkpoint identity and summary fields must not be empty");
    }
    provider_recovery_detail_validate_failure(
        checkpoint.failure_attribution, diagnostics, "provider write recovery checkpoint");
    if (checkpoint.recovery_eligibility != ProviderWriteRecoveryEligibility::NotRequired &&
        !checkpoint.failure_attribution.has_value()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery checkpoint recoverable status requires failure_attribution");
    }
    return result;
}

ProviderWriteRecoveryPlanValidationResult
validate_provider_write_recovery_plan(const ProviderWriteRecoveryPlan &plan) {
    ProviderWriteRecoveryPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderWriteRecoveryPlanFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery plan format_version must be '" +
                std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_write_recovery_checkpoint_format_version !=
        kProviderWriteRecoveryCheckpointFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery plan source format_version must be '" +
                std::string(kProviderWriteRecoveryCheckpointFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_write_recovery_checkpoint_identity.empty() ||
        plan.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        plan.partial_write_recovery_plan.empty() ||
        plan.resume_token_placeholder_identity.empty()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics, "provider write recovery plan identity and plan fields must not be empty");
    }
    if (!plan.secret_free) {
        provider_recovery_detail_emit_validation_error(
            diagnostics, "provider write recovery plan must be secret-free");
    }
    provider_recovery_detail_validate_failure(
        plan.failure_attribution, diagnostics, "provider write recovery plan");
    if (plan.plan_action != ProviderWriteRecoveryPlanAction::NoopCommitted &&
        !plan.failure_attribution.has_value()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery plan non-noop action requires failure_attribution");
    }
    return result;
}

ProviderWriteRecoveryReviewValidationResult
validate_provider_write_recovery_review(const ProviderWriteRecoveryReview &review) {
    ProviderWriteRecoveryReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderWriteRecoveryReviewFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery review format_version must be '" +
                std::string(kProviderWriteRecoveryReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_recovery_plan_format_version !=
        kProviderWriteRecoveryPlanFormatVersion) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery review source format_version must be '" +
                std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        review.recovery_review_summary.empty() || review.next_step_recommendation.empty()) {
        provider_recovery_detail_emit_validation_error(
            diagnostics,
            "provider write recovery review identity and summary fields must not be empty");
    }
    provider_recovery_detail_validate_failure(
        review.failure_attribution, diagnostics, "provider write recovery review");
    return result;
}

ProviderWriteRecoveryCheckpointResult
build_provider_write_recovery_checkpoint(const ProviderWriteCommitReceipt &receipt) {
    ProviderWriteRecoveryCheckpointResult result;
    result.diagnostics.append(validate_provider_write_commit_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderWriteRecoveryCheckpoint checkpoint;
    checkpoint.workflow_canonical_name = receipt.workflow_canonical_name;
    checkpoint.session_id = receipt.session_id;
    checkpoint.run_id = receipt.run_id;
    checkpoint.input_fixture = receipt.input_fixture;
    checkpoint.durable_store_import_provider_write_commit_receipt_identity =
        receipt.durable_store_import_provider_write_commit_receipt_identity;
    checkpoint.source_commit_status = receipt.commit_status;
    checkpoint.recovery_checkpoint_reference =
        "provider-recovery-checkpoint::" + receipt.session_id;
    checkpoint.resume_token_placeholder_identity =
        "provider-write-resume-token-placeholder::" + receipt.idempotency_key;
    checkpoint.idempotency_key = receipt.idempotency_key;

    switch (receipt.commit_status) {
    case ProviderWriteCommitStatus::Committed:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::StableCommitted;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::NotRequired;
        checkpoint.recovery_summary = "provider write committed; no recovery is required";
        break;
    case ProviderWriteCommitStatus::Duplicate:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::DuplicateReview;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::DuplicateLookupRequired;
        checkpoint.recovery_summary =
            "duplicate commit requires provider commit lookup before resume";
        checkpoint.failure_attribution = provider_recovery_detail_recovery_failure(
            ProviderWriteRecoveryFailureKind::DuplicateCommitUnresolved,
            "provider duplicate commit must be resolved before recovery");
        break;
    case ProviderWriteCommitStatus::Partial:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::PartialWrite;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::ResumeRequired;
        checkpoint.recovery_summary = "partial provider write can resume with idempotency key";
        checkpoint.failure_attribution = provider_recovery_detail_recovery_failure(
            ProviderWriteRecoveryFailureKind::PartialWrite,
            "provider write is partial and requires resume token");
        break;
    case ProviderWriteCommitStatus::Failed:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::FailedWrite;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::ManualRecoveryRequired;
        checkpoint.recovery_summary = "failed provider write requires manual remediation";
        checkpoint.failure_attribution = provider_recovery_detail_recovery_failure(
            ProviderWriteRecoveryFailureKind::ProviderFailure,
            "provider write failed before recoverable commit");
        break;
    case ProviderWriteCommitStatus::Blocked:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::Blocked;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::Blocked;
        checkpoint.recovery_summary = "commit receipt is blocked; recovery checkpoint is blocked";
        checkpoint.failure_attribution = provider_recovery_detail_recovery_failure(
            ProviderWriteRecoveryFailureKind::CommitReceiptNotReady,
            "commit receipt was not ready for recovery checkpoint");
        break;
    }

    checkpoint.durable_store_import_provider_write_recovery_checkpoint_identity =
        "durable-store-import-provider-write-recovery-checkpoint::" + receipt.session_id +
        "::" + provider_recovery_detail_checkpoint_slug(checkpoint.checkpoint_status);
    result.diagnostics.append(validate_provider_write_recovery_checkpoint(checkpoint).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.checkpoint = std::move(checkpoint);
    return result;
}

ProviderWriteRecoveryPlanResult
build_provider_write_recovery_plan(const ProviderWriteRecoveryCheckpoint &checkpoint) {
    ProviderWriteRecoveryPlanResult result;
    result.diagnostics.append(validate_provider_write_recovery_checkpoint(checkpoint).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderWriteRecoveryPlan plan;
    plan.workflow_canonical_name = checkpoint.workflow_canonical_name;
    plan.session_id = checkpoint.session_id;
    plan.run_id = checkpoint.run_id;
    plan.input_fixture = checkpoint.input_fixture;
    plan.durable_store_import_provider_write_recovery_checkpoint_identity =
        checkpoint.durable_store_import_provider_write_recovery_checkpoint_identity;
    plan.recovery_eligibility = checkpoint.recovery_eligibility;
    plan.resume_token_placeholder_identity = checkpoint.resume_token_placeholder_identity;
    plan.failure_attribution = checkpoint.failure_attribution;

    switch (checkpoint.recovery_eligibility) {
    case ProviderWriteRecoveryEligibility::NotRequired:
        plan.plan_action = ProviderWriteRecoveryPlanAction::NoopCommitted;
        plan.partial_write_recovery_plan = "no recovery action; provider write is committed";
        break;
    case ProviderWriteRecoveryEligibility::DuplicateLookupRequired:
        plan.plan_action = ProviderWriteRecoveryPlanAction::LookupDuplicateCommit;
        plan.requires_provider_commit_lookup = true;
        plan.requires_operator_approval = true;
        plan.partial_write_recovery_plan =
            "lookup provider commit reference before deciding whether resume is safe";
        break;
    case ProviderWriteRecoveryEligibility::ResumeRequired:
        plan.plan_action = ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey;
        plan.partial_write_recovery_plan =
            "resume provider write using idempotency key and resume token placeholder";
        break;
    case ProviderWriteRecoveryEligibility::ManualRecoveryRequired:
        plan.plan_action = ProviderWriteRecoveryPlanAction::ManualRemediation;
        plan.requires_operator_approval = true;
        plan.partial_write_recovery_plan =
            "manual remediation required before provider write can be retried";
        break;
    case ProviderWriteRecoveryEligibility::Blocked:
        plan.plan_action = ProviderWriteRecoveryPlanAction::WaitForCommitReceipt;
        plan.requires_operator_approval = true;
        plan.partial_write_recovery_plan =
            "wait for commit receipt before recovery planning can continue";
        break;
    }

    plan.durable_store_import_provider_write_recovery_plan_identity =
        "durable-store-import-provider-write-recovery-plan::" + checkpoint.session_id +
        "::" + provider_recovery_detail_plan_action_slug(plan.plan_action);
    result.diagnostics.append(validate_provider_write_recovery_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderWriteRecoveryReviewResult
build_provider_write_recovery_review(const ProviderWriteRecoveryPlan &plan) {
    ProviderWriteRecoveryReviewResult result;
    result.diagnostics.append(validate_provider_write_recovery_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderWriteRecoveryReview review;
    review.workflow_canonical_name = plan.workflow_canonical_name;
    review.session_id = plan.session_id;
    review.run_id = plan.run_id;
    review.input_fixture = plan.input_fixture;
    review.durable_store_import_provider_write_recovery_plan_identity =
        plan.durable_store_import_provider_write_recovery_plan_identity;
    review.plan_action = plan.plan_action;
    review.recovery_review_summary = "provider write recovery action is " +
                                     provider_recovery_detail_plan_action_slug(plan.plan_action);
    review.next_action = provider_recovery_detail_next_action_for_plan(plan.plan_action);
    review.next_step_recommendation =
        review.next_action == ProviderWriteRecoveryNextActionKind::ReadyForAuditEvent
            ? "ready for provider execution audit event"
            : "operator review or idempotent resume is required";
    review.failure_attribution = plan.failure_attribution;

    result.diagnostics.append(validate_provider_write_recovery_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_failure_taxonomy.cpp ----

#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_failure_taxonomy_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_FAILURE_TAXONOMY";

void provider_failure_taxonomy_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                            std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_failure_taxonomy_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_failure_taxonomy_detail_failure_kind_slug(ProviderFailureKindV1 kind) {
    switch (kind) {
    case ProviderFailureKindV1::None:
        return "none";
    case ProviderFailureKindV1::Authentication:
        return "authentication";
    case ProviderFailureKindV1::Authorization:
        return "authorization";
    case ProviderFailureKindV1::Network:
        return "network";
    case ProviderFailureKindV1::Throttling:
        return "throttling";
    case ProviderFailureKindV1::Conflict:
        return "conflict";
    case ProviderFailureKindV1::SchemaMismatch:
        return "schema-mismatch";
    case ProviderFailureKindV1::ProviderInternal:
        return "provider-internal";
    case ProviderFailureKindV1::Unknown:
        return "unknown";
    case ProviderFailureKindV1::Blocked:
        return "blocked";
    }
    return "unknown";
}

[[nodiscard]] std::string
provider_failure_taxonomy_detail_action_summary(ProviderFailureOperatorActionV1 action) {
    switch (action) {
    case ProviderFailureOperatorActionV1::None:
        return "no operator action required";
    case ProviderFailureOperatorActionV1::RotateCredentials:
        return "rotate credential handle before retry";
    case ProviderFailureOperatorActionV1::GrantPermission:
        return "grant provider permission before retry";
    case ProviderFailureOperatorActionV1::RetryLater:
        return "retry later with idempotency token";
    case ProviderFailureOperatorActionV1::InspectDuplicate:
        return "inspect provider commit for duplicate write";
    case ProviderFailureOperatorActionV1::FixSchema:
        return "fix payload schema before retry";
    case ProviderFailureOperatorActionV1::EscalateProvider:
        return "escalate provider internal error";
    case ProviderFailureOperatorActionV1::ManualReview:
        return "manual review required";
    }
    return "manual review required";
}

void provider_failure_taxonomy_detail_apply_mapping(
    ProviderSdkMockAdapterNormalizedResultKind result_kind, ProviderFailureTaxonomyReport &report) {
    switch (result_kind) {
    case ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        report.failure_kind = ProviderFailureKindV1::None;
        report.failure_category = ProviderFailureCategoryV1::None;
        report.retryability = ProviderFailureRetryabilityV1::NotApplicable;
        report.operator_action = ProviderFailureOperatorActionV1::None;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::None;
        report.redacted_failure_summary = "accepted provider result has no failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Timeout:
        report.failure_kind = ProviderFailureKindV1::Network;
        report.failure_category = ProviderFailureCategoryV1::Transport;
        report.retryability = ProviderFailureRetryabilityV1::Retryable;
        report.operator_action = ProviderFailureOperatorActionV1::RetryLater;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider timeout mapped to retryable network failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        report.failure_kind = ProviderFailureKindV1::Throttling;
        report.failure_category = ProviderFailureCategoryV1::RateLimit;
        report.retryability = ProviderFailureRetryabilityV1::Retryable;
        report.operator_action = ProviderFailureOperatorActionV1::RetryLater;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary =
            "provider throttle mapped to retryable rate limit failure";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        report.failure_kind = ProviderFailureKindV1::Conflict;
        report.failure_category = ProviderFailureCategoryV1::Consistency;
        report.retryability = ProviderFailureRetryabilityV1::DuplicateReviewRequired;
        report.operator_action = ProviderFailureOperatorActionV1::InspectDuplicate;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider conflict mapped to duplicate review";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        report.failure_kind = ProviderFailureKindV1::SchemaMismatch;
        report.failure_category = ProviderFailureCategoryV1::Contract;
        report.retryability = ProviderFailureRetryabilityV1::NonRetryable;
        report.operator_action = ProviderFailureOperatorActionV1::FixSchema;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::NonSensitive;
        report.redacted_failure_summary = "provider schema mismatch requires payload fix";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
        report.failure_kind = ProviderFailureKindV1::ProviderInternal;
        report.failure_category = ProviderFailureCategoryV1::Provider;
        report.retryability = ProviderFailureRetryabilityV1::NonRetryable;
        report.operator_action = ProviderFailureOperatorActionV1::EscalateProvider;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::Unknown;
        report.redacted_failure_summary = "provider internal failure requires escalation";
        return;
    case ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        report.failure_kind = ProviderFailureKindV1::Blocked;
        report.failure_category = ProviderFailureCategoryV1::Blocked;
        report.retryability = ProviderFailureRetryabilityV1::Blocked;
        report.operator_action = ProviderFailureOperatorActionV1::ManualReview;
        report.security_sensitivity = ProviderFailureSecuritySensitivityV1::Unknown;
        report.redacted_failure_summary = "provider result blocked before failure taxonomy mapping";
        return;
    }
}

} // namespace

ProviderFailureTaxonomyReportValidationResult
validate_provider_failure_taxonomy_report(const ProviderFailureTaxonomyReport &report) {
    ProviderFailureTaxonomyReportValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (report.format_version != kProviderFailureTaxonomyReportFormatVersion) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy report format_version must be '" +
                std::string(kProviderFailureTaxonomyReportFormatVersion) + "'");
    }
    if (report
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy report adapter source format_version must "
            "be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (report.source_durable_store_import_provider_write_recovery_plan_format_version !=
        kProviderWriteRecoveryPlanFormatVersion) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy report recovery source format_version "
            "must be '" +
                std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        report.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        report.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        report.redacted_failure_summary.empty() || report.recovery_review_failure_summary.empty()) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy report identity and summary fields must not be empty");
    }
    if (report.secret_bearing_error_persisted || report.raw_provider_error_persisted) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy report must not persist secret-bearing or "
            "raw provider errors");
    }
    return result;
}

ProviderFailureTaxonomyReviewValidationResult
validate_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReview &review) {
    ProviderFailureTaxonomyReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderFailureTaxonomyReviewFormatVersion) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy review format_version must be '" +
                std::string(kProviderFailureTaxonomyReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_failure_taxonomy_report_format_version !=
        kProviderFailureTaxonomyReportFormatVersion) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy review source format_version must be '" +
                std::string(kProviderFailureTaxonomyReportFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_failure_taxonomy_report_identity.empty() ||
        review.taxonomy_review_summary.empty() || review.operator_recommendation.empty()) {
        provider_failure_taxonomy_detail_emit_validation_error(
            diagnostics,
            "provider failure taxonomy review identity and summary fields must not be empty");
    }
    return result;
}

ProviderFailureTaxonomyReportResult
build_provider_failure_taxonomy_report(const ProviderSdkMockAdapterExecutionResult &adapter_result,
                                       const ProviderWriteRecoveryPlan &recovery_plan) {
    ProviderFailureTaxonomyReportResult result;
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(adapter_result).diagnostics);
    result.diagnostics.append(validate_provider_write_recovery_plan(recovery_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFailureTaxonomyReport report;
    report.workflow_canonical_name = adapter_result.workflow_canonical_name;
    report.session_id = adapter_result.session_id;
    report.run_id = adapter_result.run_id;
    report.input_fixture = adapter_result.input_fixture;
    report.durable_store_import_provider_sdk_mock_adapter_result_identity =
        adapter_result.durable_store_import_provider_sdk_mock_adapter_result_identity;
    report.durable_store_import_provider_write_recovery_plan_identity =
        recovery_plan.durable_store_import_provider_write_recovery_plan_identity;
    report.source_normalized_result = adapter_result.normalized_result;
    provider_failure_taxonomy_detail_apply_mapping(adapter_result.normalized_result, report);
    report.recovery_review_failure_summary = recovery_plan.partial_write_recovery_plan;
    report.durable_store_import_provider_failure_taxonomy_report_identity =
        "durable-store-import-provider-failure-taxonomy-report::" + report.session_id +
        "::" + provider_failure_taxonomy_detail_failure_kind_slug(report.failure_kind);

    result.diagnostics.append(validate_provider_failure_taxonomy_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

ProviderFailureTaxonomyReviewResult
build_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReport &report) {
    ProviderFailureTaxonomyReviewResult result;
    result.diagnostics.append(validate_provider_failure_taxonomy_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFailureTaxonomyReview review;
    review.workflow_canonical_name = report.workflow_canonical_name;
    review.session_id = report.session_id;
    review.run_id = report.run_id;
    review.input_fixture = report.input_fixture;
    review.durable_store_import_provider_failure_taxonomy_report_identity =
        report.durable_store_import_provider_failure_taxonomy_report_identity;
    review.failure_kind = report.failure_kind;
    review.retryability = report.retryability;
    review.operator_action = report.operator_action;
    review.security_sensitivity = report.security_sensitivity;
    review.taxonomy_review_summary =
        "provider failure taxonomy kind is " +
        provider_failure_taxonomy_detail_failure_kind_slug(report.failure_kind);
    review.operator_recommendation =
        provider_failure_taxonomy_detail_action_summary(report.operator_action);

    result.diagnostics.append(validate_provider_failure_taxonomy_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

#include "ahfl/durable_store_import/provider_commit.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_WRITE_COMMIT";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string commit_slug(ProviderWriteCommitStatus status) {
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

void validate_failure(const std::optional<ProviderWriteCommitFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

[[nodiscard]] ProviderWriteCommitFailureAttribution
commit_failure(ProviderWriteCommitFailureKind kind, std::string message) {
    return ProviderWriteCommitFailureAttribution{
        .kind = kind,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderWriteCommitNextActionKind
next_action_for_receipt(const ProviderWriteCommitReceipt &receipt) {
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
        emit_validation_error(diagnostics,
                              "provider write commit receipt format_version must be '" +
                                  std::string(kProviderWriteCommitReceiptFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_write_retry_decision_format_version !=
        kProviderWriteRetryDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write commit receipt source format_version must be '" +
                                  std::string(kProviderWriteRetryDecisionFormatVersion) + "'");
    }
    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty() ||
        receipt.input_fixture.empty() ||
        receipt.durable_store_import_provider_write_retry_decision_identity.empty() ||
        receipt.durable_store_import_provider_write_commit_receipt_identity.empty() ||
        receipt.provider_commit_reference.empty() || receipt.provider_commit_digest.empty() ||
        receipt.idempotency_key.empty()) {
        emit_validation_error(diagnostics,
                              "provider write commit receipt identity fields must not be empty");
    }
    if (!receipt.secret_free || receipt.raw_provider_payload_persisted) {
        emit_validation_error(diagnostics,
                              "provider write commit receipt must be secret-free and cannot "
                              "persist raw provider payload");
    }
    validate_failure(receipt.failure_attribution, diagnostics, "provider write commit receipt");
    if (receipt.commit_status != ProviderWriteCommitStatus::Committed &&
        !receipt.failure_attribution.has_value()) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "provider write commit review format_version must be '" +
                                  std::string(kProviderWriteCommitReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_commit_receipt_format_version !=
        kProviderWriteCommitReceiptFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write commit review source format_version must be '" +
                                  std::string(kProviderWriteCommitReceiptFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_commit_receipt_identity.empty() ||
        review.provider_commit_reference.empty() || review.provider_commit_digest.empty() ||
        review.commit_review_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider write commit review identity and summary fields must not be empty");
    }
    validate_failure(review.failure_attribution, diagnostics, "provider write commit review");
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
        receipt.failure_attribution =
            commit_failure(ProviderWriteCommitFailureKind::DuplicateDetected,
                           "provider conflict mapped to duplicate commit review");
        break;
    case ProviderWriteRetryEligibility::Retryable:
        receipt.commit_status = ProviderWriteCommitStatus::Partial;
        receipt.provider_commit_reference =
            "provider-commit::" + decision.session_id + "::partial-retryable";
        receipt.provider_commit_digest = "sha256:provider-commit-partial:" + decision.session_id;
        receipt.failure_attribution =
            commit_failure(ProviderWriteCommitFailureKind::PartialCommit,
                           "provider write requires retry under idempotency contract");
        break;
    case ProviderWriteRetryEligibility::NonRetryable:
        receipt.commit_status = ProviderWriteCommitStatus::Failed;
        receipt.provider_commit_reference = "provider-commit::" + decision.session_id + "::failed";
        receipt.provider_commit_digest = "sha256:provider-commit-failed:" + decision.session_id;
        receipt.failure_attribution =
            commit_failure(ProviderWriteCommitFailureKind::ProviderFailure,
                           "provider failure cannot be committed without manual remediation");
        break;
    case ProviderWriteRetryEligibility::Blocked:
        receipt.commit_status = ProviderWriteCommitStatus::Blocked;
        receipt.provider_commit_reference = "provider-commit::" + decision.session_id + "::blocked";
        receipt.provider_commit_digest = "sha256:provider-commit-blocked:" + decision.session_id;
        receipt.failure_attribution = commit_failure(
            ProviderWriteCommitFailureKind::RetryDecisionNotReady, "retry decision is blocked");
        break;
    }
    receipt.durable_store_import_provider_write_commit_receipt_identity =
        "durable-store-import-provider-write-commit-receipt::" + decision.session_id +
        "::" + commit_slug(receipt.commit_status);
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
    review.commit_review_summary =
        "provider write commit receipt status is " + commit_slug(receipt.commit_status);
    review.next_action = next_action_for_receipt(receipt);
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

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_retry.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderWriteCommitReceiptFormatVersion =
    "ahfl.durable-store-import-provider-write-commit-receipt.v1";

inline constexpr std::string_view kProviderWriteCommitReviewFormatVersion =
    "ahfl.durable-store-import-provider-write-commit-review.v1";

enum class ProviderWriteCommitStatus {
    Committed,
    Duplicate,
    Partial,
    Failed,
    Blocked,
};

enum class ProviderWriteCommitFailureKind {
    AdapterResultNotReady,
    RetryDecisionNotReady,
    ProviderFailure,
    DuplicateDetected,
    PartialCommit,
};

enum class ProviderWriteCommitNextActionKind {
    ReadyForRecoveryAudit,
    RetryUsingIdempotencyContract,
    ManualReviewRequired,
};

struct ProviderWriteCommitFailureAttribution {
    ProviderWriteCommitFailureKind kind{ProviderWriteCommitFailureKind::AdapterResultNotReady};
    std::string message;
};

struct ProviderWriteCommitReceipt {
    std::string format_version{std::string(kProviderWriteCommitReceiptFormatVersion)};
    std::string source_durable_store_import_provider_write_retry_decision_format_version{
        std::string(kProviderWriteRetryDecisionFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_retry_decision_identity;
    std::string durable_store_import_provider_write_commit_receipt_identity;
    ProviderWriteCommitStatus commit_status{ProviderWriteCommitStatus::Blocked};
    std::string provider_commit_reference;
    std::string provider_commit_digest;
    std::string idempotency_key;
    bool secret_free{true};
    bool raw_provider_payload_persisted{false};
    std::optional<ProviderWriteCommitFailureAttribution> failure_attribution;
};

struct ProviderWriteCommitReview {
    std::string format_version{std::string(kProviderWriteCommitReviewFormatVersion)};
    std::string source_durable_store_import_provider_write_commit_receipt_format_version{
        std::string(kProviderWriteCommitReceiptFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_commit_receipt_identity;
    ProviderWriteCommitStatus commit_status{ProviderWriteCommitStatus::Blocked};
    std::string provider_commit_reference;
    std::string provider_commit_digest;
    std::string commit_review_summary;
    std::string next_step_recommendation;
    ProviderWriteCommitNextActionKind next_action{
        ProviderWriteCommitNextActionKind::ManualReviewRequired};
    std::optional<ProviderWriteCommitFailureAttribution> failure_attribution;
};

struct ProviderWriteCommitReceiptValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteCommitReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteCommitReceiptResult {
    std::optional<ProviderWriteCommitReceipt> receipt;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteCommitReviewResult {
    std::optional<ProviderWriteCommitReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderWriteCommitReceiptValidationResult
validate_provider_write_commit_receipt(const ProviderWriteCommitReceipt &receipt);

[[nodiscard]] ProviderWriteCommitReviewValidationResult
validate_provider_write_commit_review(const ProviderWriteCommitReview &review);

[[nodiscard]] ProviderWriteCommitReceiptResult
build_provider_write_commit_receipt(const ProviderWriteRetryDecision &decision);

[[nodiscard]] ProviderWriteCommitReviewResult
build_provider_write_commit_review(const ProviderWriteCommitReceipt &receipt);

} // namespace ahfl::durable_store_import

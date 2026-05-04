#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_commit.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderWriteRecoveryCheckpointFormatVersion =
    "ahfl.durable-store-import-provider-write-recovery-checkpoint.v1";

inline constexpr std::string_view kProviderWriteRecoveryPlanFormatVersion =
    "ahfl.durable-store-import-provider-write-recovery-plan.v1";

inline constexpr std::string_view kProviderWriteRecoveryReviewFormatVersion =
    "ahfl.durable-store-import-provider-write-recovery-review.v1";

inline constexpr std::string_view kProviderWriteResumeTokenVersion =
    "ahfl.provider-write-resume-token.v1";

enum class ProviderWriteRecoveryCheckpointStatus {
    StableCommitted,
    DuplicateReview,
    PartialWrite,
    FailedWrite,
    Blocked,
};

enum class ProviderWriteRecoveryEligibility {
    NotRequired,
    ResumeRequired,
    DuplicateLookupRequired,
    ManualRecoveryRequired,
    Blocked,
};

enum class ProviderWriteRecoveryPlanAction {
    NoopCommitted,
    LookupDuplicateCommit,
    ResumeWithIdempotencyKey,
    ManualRemediation,
    WaitForCommitReceipt,
};

enum class ProviderWriteRecoveryFailureKind {
    CommitReceiptNotReady,
    PartialWrite,
    DuplicateCommitUnresolved,
    ProviderFailure,
    Blocked,
};

enum class ProviderWriteRecoveryNextActionKind {
    ReadyForAuditEvent,
    ResumeUsingToken,
    ManualReviewRequired,
};

struct ProviderWriteRecoveryFailureAttribution {
    ProviderWriteRecoveryFailureKind kind{ProviderWriteRecoveryFailureKind::CommitReceiptNotReady};
    std::string message;
};

struct ProviderWriteRecoveryCheckpoint {
    std::string format_version{std::string(kProviderWriteRecoveryCheckpointFormatVersion)};
    std::string source_durable_store_import_provider_write_commit_receipt_format_version{
        std::string(kProviderWriteCommitReceiptFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_commit_receipt_identity;
    ProviderWriteCommitStatus source_commit_status{ProviderWriteCommitStatus::Blocked};
    std::string durable_store_import_provider_write_recovery_checkpoint_identity;
    std::string recovery_checkpoint_reference;
    std::string resume_token_version{std::string(kProviderWriteResumeTokenVersion)};
    std::string resume_token_placeholder_identity;
    std::string idempotency_key;
    ProviderWriteRecoveryCheckpointStatus checkpoint_status{
        ProviderWriteRecoveryCheckpointStatus::Blocked};
    ProviderWriteRecoveryEligibility recovery_eligibility{
        ProviderWriteRecoveryEligibility::Blocked};
    std::string recovery_summary;
    std::optional<ProviderWriteRecoveryFailureAttribution> failure_attribution;
};

struct ProviderWriteRecoveryPlan {
    std::string format_version{std::string(kProviderWriteRecoveryPlanFormatVersion)};
    std::string source_durable_store_import_provider_write_recovery_checkpoint_format_version{
        std::string(kProviderWriteRecoveryCheckpointFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_recovery_checkpoint_identity;
    ProviderWriteRecoveryEligibility recovery_eligibility{
        ProviderWriteRecoveryEligibility::Blocked};
    std::string durable_store_import_provider_write_recovery_plan_identity;
    ProviderWriteRecoveryPlanAction plan_action{
        ProviderWriteRecoveryPlanAction::WaitForCommitReceipt};
    std::string partial_write_recovery_plan;
    std::string resume_token_placeholder_identity;
    bool requires_provider_commit_lookup{false};
    bool requires_operator_approval{false};
    bool secret_free{true};
    std::optional<ProviderWriteRecoveryFailureAttribution> failure_attribution;
};

struct ProviderWriteRecoveryReview {
    std::string format_version{std::string(kProviderWriteRecoveryReviewFormatVersion)};
    std::string source_durable_store_import_provider_write_recovery_plan_format_version{
        std::string(kProviderWriteRecoveryPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_write_recovery_plan_identity;
    ProviderWriteRecoveryPlanAction plan_action{
        ProviderWriteRecoveryPlanAction::WaitForCommitReceipt};
    std::string recovery_review_summary;
    std::string next_step_recommendation;
    ProviderWriteRecoveryNextActionKind next_action{
        ProviderWriteRecoveryNextActionKind::ManualReviewRequired};
    std::optional<ProviderWriteRecoveryFailureAttribution> failure_attribution;
};

struct ProviderWriteRecoveryCheckpointValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRecoveryPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRecoveryReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRecoveryCheckpointResult {
    std::optional<ProviderWriteRecoveryCheckpoint> checkpoint;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRecoveryPlanResult {
    std::optional<ProviderWriteRecoveryPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteRecoveryReviewResult {
    std::optional<ProviderWriteRecoveryReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderWriteRecoveryCheckpointValidationResult
validate_provider_write_recovery_checkpoint(const ProviderWriteRecoveryCheckpoint &checkpoint);

[[nodiscard]] ProviderWriteRecoveryPlanValidationResult
validate_provider_write_recovery_plan(const ProviderWriteRecoveryPlan &plan);

[[nodiscard]] ProviderWriteRecoveryReviewValidationResult
validate_provider_write_recovery_review(const ProviderWriteRecoveryReview &review);

[[nodiscard]] ProviderWriteRecoveryCheckpointResult
build_provider_write_recovery_checkpoint(const ProviderWriteCommitReceipt &receipt);

[[nodiscard]] ProviderWriteRecoveryPlanResult
build_provider_write_recovery_plan(const ProviderWriteRecoveryCheckpoint &checkpoint);

[[nodiscard]] ProviderWriteRecoveryReviewResult
build_provider_write_recovery_review(const ProviderWriteRecoveryPlan &plan);

} // namespace ahfl::durable_store_import

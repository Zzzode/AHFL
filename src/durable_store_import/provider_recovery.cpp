#include "ahfl/durable_store_import/provider_recovery.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_WRITE_RECOVERY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string checkpoint_slug(ProviderWriteRecoveryCheckpointStatus status) {
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

[[nodiscard]] std::string plan_action_slug(ProviderWriteRecoveryPlanAction action) {
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
recovery_failure(ProviderWriteRecoveryFailureKind kind, std::string message) {
    return ProviderWriteRecoveryFailureAttribution{
        .kind = kind,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderWriteRecoveryFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

[[nodiscard]] ProviderWriteRecoveryNextActionKind
next_action_for_plan(ProviderWriteRecoveryPlanAction action) {
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
        emit_validation_error(diagnostics,
                              "provider write recovery checkpoint format_version must be '" +
                                  std::string(kProviderWriteRecoveryCheckpointFormatVersion) + "'");
    }
    if (checkpoint.source_durable_store_import_provider_write_commit_receipt_format_version !=
        kProviderWriteCommitReceiptFormatVersion) {
        emit_validation_error(diagnostics,
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
        emit_validation_error(
            diagnostics,
            "provider write recovery checkpoint identity and summary fields must not be empty");
    }
    validate_failure(
        checkpoint.failure_attribution, diagnostics, "provider write recovery checkpoint");
    if (checkpoint.recovery_eligibility != ProviderWriteRecoveryEligibility::NotRequired &&
        !checkpoint.failure_attribution.has_value()) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "provider write recovery plan format_version must be '" +
                                  std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_write_recovery_checkpoint_format_version !=
        kProviderWriteRecoveryCheckpointFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write recovery plan source format_version must be '" +
                                  std::string(kProviderWriteRecoveryCheckpointFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_write_recovery_checkpoint_identity.empty() ||
        plan.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        plan.partial_write_recovery_plan.empty() ||
        plan.resume_token_placeholder_identity.empty()) {
        emit_validation_error(
            diagnostics, "provider write recovery plan identity and plan fields must not be empty");
    }
    if (!plan.secret_free) {
        emit_validation_error(diagnostics, "provider write recovery plan must be secret-free");
    }
    validate_failure(plan.failure_attribution, diagnostics, "provider write recovery plan");
    if (plan.plan_action != ProviderWriteRecoveryPlanAction::NoopCommitted &&
        !plan.failure_attribution.has_value()) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "provider write recovery review format_version must be '" +
                                  std::string(kProviderWriteRecoveryReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_recovery_plan_format_version !=
        kProviderWriteRecoveryPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider write recovery review source format_version must be '" +
                                  std::string(kProviderWriteRecoveryPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_recovery_plan_identity.empty() ||
        review.recovery_review_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider write recovery review identity and summary fields must not be empty");
    }
    validate_failure(review.failure_attribution, diagnostics, "provider write recovery review");
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
        checkpoint.failure_attribution =
            recovery_failure(ProviderWriteRecoveryFailureKind::DuplicateCommitUnresolved,
                             "provider duplicate commit must be resolved before recovery");
        break;
    case ProviderWriteCommitStatus::Partial:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::PartialWrite;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::ResumeRequired;
        checkpoint.recovery_summary = "partial provider write can resume with idempotency key";
        checkpoint.failure_attribution =
            recovery_failure(ProviderWriteRecoveryFailureKind::PartialWrite,
                             "provider write is partial and requires resume token");
        break;
    case ProviderWriteCommitStatus::Failed:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::FailedWrite;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::ManualRecoveryRequired;
        checkpoint.recovery_summary = "failed provider write requires manual remediation";
        checkpoint.failure_attribution =
            recovery_failure(ProviderWriteRecoveryFailureKind::ProviderFailure,
                             "provider write failed before recoverable commit");
        break;
    case ProviderWriteCommitStatus::Blocked:
        checkpoint.checkpoint_status = ProviderWriteRecoveryCheckpointStatus::Blocked;
        checkpoint.recovery_eligibility = ProviderWriteRecoveryEligibility::Blocked;
        checkpoint.recovery_summary = "commit receipt is blocked; recovery checkpoint is blocked";
        checkpoint.failure_attribution =
            recovery_failure(ProviderWriteRecoveryFailureKind::CommitReceiptNotReady,
                             "commit receipt was not ready for recovery checkpoint");
        break;
    }

    checkpoint.durable_store_import_provider_write_recovery_checkpoint_identity =
        "durable-store-import-provider-write-recovery-checkpoint::" + receipt.session_id +
        "::" + checkpoint_slug(checkpoint.checkpoint_status);
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
        "::" + plan_action_slug(plan.plan_action);
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
    review.recovery_review_summary =
        "provider write recovery action is " + plan_action_slug(plan.plan_action);
    review.next_action = next_action_for_plan(plan.plan_action);
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

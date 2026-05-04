#include "ahfl/backends/durable_store_import_provider_write_recovery_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_action(durable_store_import::ProviderWriteRecoveryPlanAction action, std::ostream &out) {
    switch (action) {
    case durable_store_import::ProviderWriteRecoveryPlanAction::NoopCommitted:
        out << "noop_committed";
        return;
    case durable_store_import::ProviderWriteRecoveryPlanAction::LookupDuplicateCommit:
        out << "lookup_duplicate_commit";
        return;
    case durable_store_import::ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey:
        out << "resume_with_idempotency_key";
        return;
    case durable_store_import::ProviderWriteRecoveryPlanAction::ManualRemediation:
        out << "manual_remediation";
        return;
    case durable_store_import::ProviderWriteRecoveryPlanAction::WaitForCommitReceipt:
        out << "wait_for_commit_receipt";
        return;
    }
}

void print_next(durable_store_import::ProviderWriteRecoveryNextActionKind action,
                std::ostream &out) {
    switch (action) {
    case durable_store_import::ProviderWriteRecoveryNextActionKind::ReadyForAuditEvent:
        out << "ready_for_audit_event";
        return;
    case durable_store_import::ProviderWriteRecoveryNextActionKind::ResumeUsingToken:
        out << "resume_using_token";
        return;
    case durable_store_import::ProviderWriteRecoveryNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_write_recovery_review(
    const durable_store_import::ProviderWriteRecoveryReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "recovery_plan " << review.durable_store_import_provider_write_recovery_plan_identity
        << '\n';
    out << "plan_action ";
    print_action(review.plan_action, out);
    out << '\n';
    out << "summary " << review.recovery_review_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl

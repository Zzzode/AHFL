#include "ahfl/backends/durable_store_import_provider_write_commit_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderWriteCommitStatus status, std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderWriteCommitStatus::Committed:
        out << "committed";
        return;
    case durable_store_import::ProviderWriteCommitStatus::Duplicate:
        out << "duplicate";
        return;
    case durable_store_import::ProviderWriteCommitStatus::Partial:
        out << "partial";
        return;
    case durable_store_import::ProviderWriteCommitStatus::Failed:
        out << "failed";
        return;
    case durable_store_import::ProviderWriteCommitStatus::Blocked:
        out << "blocked";
        return;
    }
}

void print_next(durable_store_import::ProviderWriteCommitNextActionKind kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderWriteCommitNextActionKind::ReadyForRecoveryAudit:
        out << "ready_for_recovery_audit";
        return;
    case durable_store_import::ProviderWriteCommitNextActionKind::RetryUsingIdempotencyContract:
        out << "retry_using_idempotency_contract";
        return;
    case durable_store_import::ProviderWriteCommitNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_write_commit_review(
    const durable_store_import::ProviderWriteCommitReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "commit_receipt " << review.durable_store_import_provider_write_commit_receipt_identity
        << '\n';
    out << "commit_status ";
    print_status(review.commit_status, out);
    out << '\n';
    out << "provider_commit_reference " << review.provider_commit_reference << '\n';
    out << "provider_commit_digest " << review.provider_commit_digest << '\n';
    out << "summary " << review.commit_review_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl

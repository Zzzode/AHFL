#include "ahfl/backends/durable_store_import_provider_operator_review_event.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_outcome(durable_store_import::ProviderAuditOutcome outcome, std::ostream &out) {
    switch (outcome) {
    case durable_store_import::ProviderAuditOutcome::Success:
        out << "success";
        return;
    case durable_store_import::ProviderAuditOutcome::Failure:
        out << "failure";
        return;
    case durable_store_import::ProviderAuditOutcome::RetryPending:
        out << "retry_pending";
        return;
    case durable_store_import::ProviderAuditOutcome::RecoveryPending:
        out << "recovery_pending";
        return;
    case durable_store_import::ProviderAuditOutcome::Blocked:
        out << "blocked";
        return;
    }
}

void print_next(durable_store_import::ProviderAuditNextActionKind action, std::ostream &out) {
    switch (action) {
    case durable_store_import::ProviderAuditNextActionKind::Archive:
        out << "archive";
        return;
    case durable_store_import::ProviderAuditNextActionKind::RetryWithIdempotency:
        out << "retry_with_idempotency";
        return;
    case durable_store_import::ProviderAuditNextActionKind::RecoveryReview:
        out << "recovery_review";
        return;
    case durable_store_import::ProviderAuditNextActionKind::OperatorReview:
        out << "operator_review";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_operator_review_event(
    const durable_store_import::ProviderOperatorReviewEvent &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "telemetry_summary " << review.durable_store_import_provider_telemetry_summary_identity
        << '\n';
    out << "outcome ";
    print_outcome(review.outcome, out);
    out << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "summary " << review.operator_review_summary << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl

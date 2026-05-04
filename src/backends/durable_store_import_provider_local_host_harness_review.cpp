#include "ahfl/backends/durable_store_import_provider_local_host_harness_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderLocalHostHarnessStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalHostHarnessStatus::Ready ? "ready"
                                                                                  : "blocked");
}

void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind,
                   std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded:
        out << "succeeded";
        return;
    case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed:
        out << "failed";
        return;
    case durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut:
        out << "timed_out";
        return;
    case durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied:
        out << "sandbox_denied";
        return;
    case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked:
        out << "blocked";
        return;
    }
}

void print_next(durable_store_import::ProviderLocalHostHarnessNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::
        ReadyForSdkPayloadMaterialization:
        out << "ready_for_sdk_payload_materialization";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::WaitForSecretPolicy:
        out << "wait_for_secret_policy";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_host_harness_review(
    const durable_store_import::ProviderLocalHostHarnessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "harness_record "
        << review.durable_store_import_provider_local_host_harness_record_identity << '\n';
    out << "record_status ";
    print_status(review.record_status, out);
    out << '\n';
    out << "outcome ";
    print_outcome(review.outcome_kind, out);
    out << '\n';
    out << "sandbox test_only=" << (review.sandbox_policy.test_only ? "true" : "false")
        << " network=" << (review.sandbox_policy.allow_network ? "true" : "false")
        << " secret=" << (review.sandbox_policy.allow_secret ? "true" : "false")
        << " filesystem_write=" << (review.sandbox_policy.allow_filesystem_write ? "true" : "false")
        << " host_env=" << (review.sandbox_policy.allow_host_environment ? "true" : "false")
        << '\n';
    out << "summary " << review.harness_boundary_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl

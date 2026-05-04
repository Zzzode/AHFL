#include "ahfl/backends/durable_store_import_provider_secret_policy_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderSecretStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSecretStatus::Ready ? "ready" : "blocked");
}

void print_operation(durable_store_import::ProviderSecretOperationKind kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest:
        out << "plan_secret_resolver_request";
        return;
    case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverResponsePlaceholder:
        out << "plan_secret_resolver_response_placeholder";
        return;
    case durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady:
        out << "noop_config_snapshot_not_ready";
        return;
    case durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady:
        out << "noop_secret_resolver_request_not_ready";
        return;
    }
}

void print_lifecycle(durable_store_import::ProviderCredentialLifecycleState state,
                     std::ostream &out) {
    switch (state) {
    case durable_store_import::ProviderCredentialLifecycleState::HandlePlanned:
        out << "handle_planned";
        return;
    case durable_store_import::ProviderCredentialLifecycleState::PlaceholderPendingResolution:
        out << "placeholder_pending_resolution";
        return;
    case durable_store_import::ProviderCredentialLifecycleState::Blocked:
        out << "blocked";
        return;
    }
}

void print_next(durable_store_import::ProviderSecretPolicyNextActionKind kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness:
        out << "ready_for_local_host_harness";
        return;
    case durable_store_import::ProviderSecretPolicyNextActionKind::WaitForConfigSnapshot:
        out << "wait_for_config_snapshot";
        return;
    case durable_store_import::ProviderSecretPolicyNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

void print_failure(const durable_store_import::ProviderSecretPolicyReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady:
        out << "config_snapshot_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady:
        out << "secret_resolver_request_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation:
        out << "secret_policy_violation ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_secret_policy_review(
    const durable_store_import::ProviderSecretPolicyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "secret_request "
        << review.durable_store_import_provider_secret_resolver_request_identity << '\n';
    out << "secret_response "
        << review.durable_store_import_provider_secret_resolver_response_identity << '\n';
    out << "response_status ";
    print_status(review.response_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "secret_handle " << review.secret_handle.handle_identity << '\n';
    out << "credential_lifecycle " << review.credential_lifecycle_version << ' ';
    print_lifecycle(review.credential_lifecycle_state, out);
    out << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " token=" << (review.materializes_token_value ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " secret_manager=" << (review.invokes_secret_manager ? "true" : "false") << '\n';
    out << "secret_policy " << review.secret_policy_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl

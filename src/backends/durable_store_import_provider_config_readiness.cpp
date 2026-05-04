#include "ahfl/backends/durable_store_import_provider_config_readiness.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderConfigStatus status, std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderConfigStatus::Ready:
        out << "ready";
        return;
    case durable_store_import::ProviderConfigStatus::Blocked:
        out << "blocked";
        return;
    }
}

void print_operation(durable_store_import::ProviderConfigOperationKind kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad:
        out << "plan_provider_config_load";
        return;
    case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder:
        out << "plan_provider_config_snapshot_placeholder";
        return;
    case durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady:
        out << "noop_adapter_interface_not_ready";
        return;
    case durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady:
        out << "noop_config_load_not_ready";
        return;
    }
}

void print_next_action(durable_store_import::ProviderConfigReadinessNextActionKind kind,
                       std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderConfigReadinessNextActionKind::ReadyForSecretHandleResolver:
        out << "ready_for_secret_handle_resolver";
        return;
    case durable_store_import::ProviderConfigReadinessNextActionKind::WaitForAdapterInterface:
        out << "wait_for_adapter_interface";
        return;
    case durable_store_import::ProviderConfigReadinessNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

void print_failure(const durable_store_import::ProviderConfigReadinessReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady:
        out << "adapter_interface_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady:
        out << "config_load_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::MissingConfigProfile:
        out << "missing_config_profile ";
        break;
    case durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema:
        out << "incompatible_config_schema ";
        break;
    case durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile:
        out << "unsupported_config_profile ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_config_readiness(
    const durable_store_import::ProviderConfigReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "config_load " << review.durable_store_import_provider_config_load_identity << '\n';
    out << "config_snapshot " << review.durable_store_import_provider_config_snapshot_identity
        << '\n';
    out << "snapshot_status ";
    print_status(review.snapshot_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "profile " << review.provider_config_profile_identity << '\n';
    out << "snapshot_placeholder " << review.provider_config_snapshot_placeholder_identity << '\n';
    out << "config_schema " << review.config_schema_version << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " sdk_payload=" << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false") << '\n';
    out << "config_boundary " << review.config_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl

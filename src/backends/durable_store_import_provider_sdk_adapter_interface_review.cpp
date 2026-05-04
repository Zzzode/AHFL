#include "ahfl/backends/durable_store_import_provider_sdk_adapter_interface_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status,
                  std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready:
        out << "ready";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked:
        out << "blocked";
        return;
    }
}

void print_operation(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind,
                     std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        PlanProviderSdkAdapterInterface:
        out << "plan_provider_sdk_adapter_interface";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        NoopSdkAdapterRequestNotReady:
        out << "noop_sdk_adapter_request_not_ready";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::NoopUnsupportedCapability:
        out << "noop_unsupported_capability";
        return;
    }
}

void print_support(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status,
                   std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported:
        out << "supported";
        return;
    case durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported:
        out << "unsupported";
        return;
    }
}

void print_error(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind,
                 std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None:
        out << "none";
        return;
    case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability:
        out << "unsupported_capability";
        return;
    case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::SdkAdapterRequestNotReady:
        out << "sdk_adapter_request_not_ready";
        return;
    }
}

void print_next_action(durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind kind,
                       std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ReadyForMockAdapterImplementation:
        out << "ready_for_mock_adapter_implementation";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        WaitForSdkAdapterRequest:
        out << "wait_for_sdk_adapter_request";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

void print_failure(const durable_store_import::ProviderSdkAdapterInterfaceReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }

    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady:
        out << "sdk_adapter_request_not_ready ";
        break;
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
        out << "unsupported_capability ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_review(
    const durable_store_import::ProviderSdkAdapterInterfaceReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "adapter_request " << review.durable_store_import_provider_sdk_adapter_request_identity
        << '\n';
    out << "interface_artifact "
        << review.durable_store_import_provider_sdk_adapter_interface_identity << '\n';
    out << "interface_status ";
    print_status(review.interface_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "registry " << review.provider_registry_identity << '\n';
    out << "adapter_descriptor " << review.adapter_descriptor_identity << '\n';
    out << "capability_descriptor " << review.capability_descriptor_identity << '\n';
    out << "capability_support ";
    print_support(review.capability_support_status, out);
    out << '\n';
    out << "error_taxonomy " << review.error_taxonomy_version << '\n';
    out << "normalized_error ";
    print_error(review.normalized_error_kind, out);
    out << '\n';
    out << "returns_placeholder_result " << (review.returns_placeholder_result ? "true" : "false")
        << '\n';
    out << "side_effects sdk_payload="
        << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " secret=" << (review.reads_secret_material ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false") << '\n';
    out << "interface_boundary " << review.interface_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl

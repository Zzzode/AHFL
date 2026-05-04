#include "ahfl/backends/durable_store_import_provider_sdk_mock_adapter_readiness.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderSdkMockAdapterStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkMockAdapterStatus::Ready ? "ready"
                                                                                : "blocked");
}

void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind,
                      std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted:
        out << "accepted";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
        out << "provider_failure";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout:
        out << "timeout";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled:
        out << "throttled";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict:
        out << "conflict";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
        out << "schema_mismatch";
        return;
    case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked:
        out << "blocked";
        return;
    }
}

void print_next(durable_store_import::ProviderSdkMockAdapterNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity:
        out << "ready_for_real_adapter_parity";
        return;
    case durable_store_import::ProviderSdkMockAdapterNextActionKind::WaitForPayload:
        out << "wait_for_payload";
        return;
    case durable_store_import::ProviderSdkMockAdapterNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_sdk_mock_adapter_readiness(
    const durable_store_import::ProviderSdkMockAdapterReadiness &readiness, std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "mock_adapter_result "
        << readiness.durable_store_import_provider_sdk_mock_adapter_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "normalized_result ";
    print_normalized(readiness.normalized_result, out);
    out << '\n';
    out << "normalization " << readiness.normalization_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl

#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_readiness.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status,
                  std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready ? "ready"
                                                                                      : "blocked");
}

void print_next(durable_store_import::ProviderLocalFilesystemAlphaNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::
        ReadyForIdempotencyContract:
        out << "ready_for_idempotency_contract";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::WaitForMockAdapter:
        out << "wait_for_mock_adapter";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_filesystem_alpha_readiness(
    const durable_store_import::ProviderLocalFilesystemAlphaReadiness &readiness,
    std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "alpha_result "
        << readiness.durable_store_import_provider_local_filesystem_alpha_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "summary " << readiness.readiness_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl

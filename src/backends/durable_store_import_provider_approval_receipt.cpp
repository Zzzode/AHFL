#include "ahfl/backends/durable_store_import_provider_approval_receipt.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_approval_receipt_json(
    const durable_store_import::ApprovalReceipt &receipt, std::ostream &out) {
    out << receipt.format_version << '\n';
    out << "workflow " << receipt.workflow_canonical_name << '\n';
    out << "session " << receipt.session_id << '\n';
    if (receipt.run_id.has_value()) {
        out << "run_id " << *receipt.run_id << '\n';
    }
    out << "approval_request " << receipt.approval_request_identity << '\n';
    out << "approval_decision " << receipt.approval_decision_identity << '\n';
    out << "final_decision " << durable_store_import::to_string_view(receipt.final_decision)
        << '\n';
    out << "release_evidence_archive " << receipt.release_evidence_archive_manifest_identity
        << '\n';
    out << "production_readiness_evidence " << receipt.production_readiness_evidence_identity
        << '\n';
    out << "is_approved " << (receipt.is_approved ? "true" : "false") << '\n';
    out << "receipt_summary " << receipt.receipt_summary << '\n';
    out << "blocking_reason_summary " << receipt.blocking_reason_summary << '\n';
}

} // namespace ahfl

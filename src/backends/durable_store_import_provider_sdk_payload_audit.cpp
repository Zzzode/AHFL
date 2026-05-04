#include "ahfl/backends/durable_store_import_provider_sdk_payload_audit.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_status(durable_store_import::ProviderSdkPayloadStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkPayloadStatus::Ready ? "ready" : "blocked");
}

void print_next(durable_store_import::ProviderSdkPayloadNextActionKind kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkPayloadNextActionKind::ReadyForMockAdapter:
        out << "ready_for_mock_adapter";
        return;
    case durable_store_import::ProviderSdkPayloadNextActionKind::WaitForLocalHarness:
        out << "wait_for_local_harness";
        return;
    case durable_store_import::ProviderSdkPayloadNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_sdk_payload_audit(
    const durable_store_import::ProviderSdkPayloadAuditSummary &audit, std::ostream &out) {
    out << audit.format_version << '\n';
    out << "workflow " << audit.workflow_canonical_name << '\n';
    out << "session " << audit.session_id << '\n';
    out << "sdk_payload_plan " << audit.durable_store_import_provider_sdk_payload_plan_identity
        << '\n';
    out << "payload_status ";
    print_status(audit.payload_status, out);
    out << '\n';
    out << "schema " << audit.provider_request_payload_schema_ref << '\n';
    out << "payload_digest " << audit.payload_digest << '\n';
    out << "fake_provider_only " << (audit.fake_provider_only ? "true" : "false") << '\n';
    out << "raw_payload_persisted " << (audit.raw_payload_persisted ? "true" : "false") << '\n';
    out << "redaction secret_free=" << (audit.redaction_summary.secret_free ? "true" : "false")
        << " credential_free=" << (audit.redaction_summary.credential_free ? "true" : "false")
        << " token_free=" << (audit.redaction_summary.token_free ? "true" : "false")
        << " redacted_field_count=" << audit.redaction_summary.redacted_field_count << '\n';
    out << "audit " << audit.audit_summary << '\n';
    out << "next_action ";
    print_next(audit.next_action, out);
    out << '\n';
    out << "next_step " << audit.next_step_recommendation << '\n';
}

} // namespace ahfl

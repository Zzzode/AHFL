#include "ahfl/backends/durable_store_import_provider_production_integration_dry_run_report.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_production_integration_dry_run_report(
    const durable_store_import::ProviderProductionIntegrationDryRunReport &report,
    std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "conformance_report " << report.conformance_report_identity << '\n';
    out << "schema_compatibility_report " << report.schema_compatibility_report_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "release_evidence_archive_manifest " << report.release_evidence_archive_manifest_identity
        << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "runtime_policy_report " << report.runtime_policy_report_identity << '\n';
    out << "total_evidence_count " << report.total_evidence_count << '\n';
    out << "valid_evidence_count " << report.valid_evidence_count << '\n';
    out << "invalid_evidence_count " << report.invalid_evidence_count << '\n';
    out << "missing_evidence_count " << report.missing_evidence_count << '\n';
    for (const auto &item : report.evidence_chain) {
        out << "evidence " << item.evidence_type
            << " present=" << (item.is_present ? "true" : "false")
            << " valid=" << (item.is_valid ? "true" : "false")
            << " fresh=" << (item.is_fresh ? "true" : "false") << '\n';
    }
    out << "readiness_state " << durable_store_import::to_string_view(report.readiness_state)
        << '\n';
    out << "blocking_item_count " << report.blocking_item_count << '\n';
    for (const auto &blocker : report.blocking_items) {
        out << "blocker " << blocker.block_type << " reason=" << blocker.block_reason << '\n';
    }
    for (const auto &action : report.next_operator_actions) {
        out << "next_action priority=" << action.priority << " type=" << action.action_type << " "
            << action.action_description << '\n';
    }
    out << "is_ready_for_controlled_rollout "
        << (report.is_ready_for_controlled_rollout ? "true" : "false") << '\n';
    out << "is_non_mutating_mode " << (report.is_non_mutating_mode ? "true" : "false") << '\n';
    out << "dry_run_summary " << report.dry_run_summary << '\n';
    out << "blocking_summary " << report.blocking_summary << '\n';
}

} // namespace ahfl

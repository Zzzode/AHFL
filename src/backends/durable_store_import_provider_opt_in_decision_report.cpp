#include "ahfl/backends/durable_store_import_provider_opt_in_decision_report.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_opt_in_decision_report(
    const durable_store_import::ProviderOptInDecisionReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    for (const auto &gate : report.gate_checks) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        if (gate.denial_reason.has_value()) {
            out << " reason=" << durable_store_import::to_string_view(*gate.denial_reason);
        }
        out << '\n';
    }
    if (report.scoped_override.has_value()) {
        out << "override_scope " << report.scoped_override->override_scope << '\n';
        out << "override_approver " << report.scoped_override->override_approver << '\n';
        out << "override_valid " << (report.scoped_override->is_valid ? "true" : "false") << '\n';
    }
    out << "is_real_provider_traffic_allowed "
        << (report.is_real_provider_traffic_allowed ? "true" : "false") << '\n';
    out << "decision_summary " << report.decision_summary << '\n';
    out << "denial_reason_summary " << report.denial_reason_summary << '\n';
}

} // namespace ahfl

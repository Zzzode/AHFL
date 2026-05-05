#include "ahfl/backends/durable_store_import_provider_runtime_policy_report.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_runtime_policy_report(
    const durable_store_import::ProviderRuntimePolicyReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "production_readiness_evidence " << report.production_readiness_evidence_identity
        << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    out << "blocking_violation_count " << report.blocking_violation_count << '\n';
    out << "warning_violation_count " << report.warning_violation_count << '\n';
    for (const auto &gate : report.policy_gates) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        for (const auto &v : gate.violations) {
            out << " violation=" << durable_store_import::to_string_view(v);
        }
        out << '\n';
    }
    out << "is_execution_permitted " << (report.is_execution_permitted ? "true" : "false") << '\n';
    out << "policy_summary " << report.policy_summary << '\n';
    out << "violation_summary " << report.violation_summary << '\n';
    out << "next_operator_action " << report.next_operator_action << '\n';
}

} // namespace ahfl

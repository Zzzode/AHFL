#include "ahfl/backends/durable_store_import_provider_production_readiness_report.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_gate(durable_store_import::ProviderProductionReleaseGate gate, std::ostream &out) {
    switch (gate) {
    case durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview:
        out << "ready_for_production_review";
        return;
    case durable_store_import::ProviderProductionReleaseGate::Conditional:
        out << "conditional";
        return;
    case durable_store_import::ProviderProductionReleaseGate::Blocked:
        out << "blocked";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_production_readiness_report(
    const durable_store_import::ProviderProductionReadinessReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "production_readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "release_gate ";
    print_gate(report.release_gate, out);
    out << '\n';
    out << "summary " << report.operator_report_summary << '\n';
    out << "next_step " << report.next_step_recommendation << '\n';
}

} // namespace ahfl

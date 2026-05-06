#include "ahfl/backends/durable_store_import_provider_conformance_report.hpp"

#include <ostream>

namespace ahfl {
namespace {

// 辅助函数：输出检查结果
void print_result(durable_store_import::ConformanceCheckResult result, std::ostream &out) {
    switch (result) {
    case durable_store_import::ConformanceCheckResult::Pass:
        out << "pass";
        return;
    case durable_store_import::ConformanceCheckResult::Fail:
        out << "fail";
        return;
    case durable_store_import::ConformanceCheckResult::Skipped:
        out << "skipped";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_conformance_report(
    const durable_store_import::ProviderConformanceReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "input_fixture " << report.input_fixture << '\n';
    out << "compatibility_report "
        << report.durable_store_import_provider_compatibility_report_identity << '\n';
    out << "registry " << report.durable_store_import_provider_registry_identity << '\n';
    out << "readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "pass_count " << report.pass_count << '\n';
    out << "fail_count " << report.fail_count << '\n';
    out << "skipped_count " << report.skipped_count << '\n';
    out << "checks " << report.checks.size() << '\n';
    for (const auto &check : report.checks) {
        out << "check " << check.check_name << ' ';
        print_result(check.result, out);
        out << ' ' << check.artifact_reference;
        if (check.failure_reason.has_value()) {
            out << " \"" << *check.failure_reason << '"';
        }
        out << '\n';
    }
    out << "summary " << report.conformance_summary << '\n';
}

} // namespace ahfl

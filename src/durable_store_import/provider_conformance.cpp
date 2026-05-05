#include "ahfl/durable_store_import/provider_conformance.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_CONFORMANCE";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

// 执行单个检查并返回检查项
[[nodiscard]] ConformanceCheckItem run_check(
    const std::string &check_name,
    bool condition,
    const std::string &artifact_reference,
    const std::string &failure_reason_if_fail) {
    ConformanceCheckItem item;
    item.check_name = check_name;
    item.artifact_reference = artifact_reference;
    if (condition) {
        item.result = ConformanceCheckResult::Pass;
    } else {
        item.result = ConformanceCheckResult::Fail;
        item.failure_reason = failure_reason_if_fail;
    }
    return item;
}

} // namespace

ProviderConformanceReportValidationResult validate_provider_conformance_report(
    const ProviderConformanceReport &report) {
    ProviderConformanceReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    // 检查 format_version
    if (report.format_version != kProviderConformanceReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider conformance report format_version must be '" +
                                  std::string(kProviderConformanceReportFormatVersion) + "'");
    }

    // 检查 source format versions
    if (report.source_durable_store_import_provider_compatibility_report_format_version !=
            kProviderCompatibilityReportFormatVersion ||
        report.source_durable_store_import_provider_registry_format_version !=
            kProviderRegistryFormatVersion ||
        report.source_durable_store_import_provider_production_readiness_evidence_format_version !=
            kProviderProductionReadinessEvidenceFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider conformance report source format_versions must match V0.40-V0.42 artifacts");
    }

    // 检查必需字段非空
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_compatibility_report_identity.empty() ||
        report.durable_store_import_provider_registry_identity.empty() ||
        report.durable_store_import_provider_production_readiness_evidence_identity.empty() ||
        report.durable_store_import_provider_conformance_report_identity.empty() ||
        report.conformance_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider conformance report identity and summary fields must not be empty");
    }

    // 检查计数非负
    if (report.pass_count < 0 || report.fail_count < 0 || report.skipped_count < 0) {
        emit_validation_error(diagnostics,
                              "provider conformance report counts cannot be negative");
    }

    return result;
}

ProviderConformanceReportResult build_provider_conformance_report(
    const ProviderCompatibilityReport &compatibility_report,
    const ProviderRegistry &registry,
    const ProviderProductionReadinessEvidence &readiness_evidence) {
    ProviderConformanceReportResult result;

    // 验证上游 artifact
    result.diagnostics.append(validate_provider_compatibility_report(compatibility_report).diagnostics);
    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    result.diagnostics.append(
        validate_provider_production_readiness_evidence(readiness_evidence).diagnostics);

    if (result.has_errors()) {
        return result;
    }

    ProviderConformanceReport report;
    report.workflow_canonical_name = compatibility_report.workflow_canonical_name;
    report.session_id = compatibility_report.session_id;
    report.run_id = compatibility_report.run_id;
    report.input_fixture = compatibility_report.input_fixture;

    // 设置上游 artifact 引用
    report.durable_store_import_provider_compatibility_report_identity =
        compatibility_report.durable_store_import_provider_compatibility_report_identity;
    report.durable_store_import_provider_registry_identity =
        registry.durable_store_import_provider_registry_identity;
    report.durable_store_import_provider_production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // 预先生成 conformance report identity（后续检查需引用）
    report.durable_store_import_provider_conformance_report_identity =
        "durable-store-import-provider-conformance-report::" + report.session_id;

    // 执行契约检查
    std::vector<ConformanceCheckItem> checks;

    // 检查 1: Compatibility Status Pass
    checks.push_back(run_check(
        "compatibility_status_passed",
        compatibility_report.status == ProviderCompatibilityStatus::Passed,
        report.durable_store_import_provider_compatibility_report_identity,
        "compatibility status is not passed"));

    // 检查 2: Mock Adapter Compatible
    checks.push_back(run_check(
        "mock_adapter_compatible",
        compatibility_report.mock_adapter_compatible,
        report.durable_store_import_provider_compatibility_report_identity,
        "mock adapter is not compatible"));

    // 检查 3: Local Filesystem Alpha Compatible
    checks.push_back(run_check(
        "local_filesystem_alpha_compatible",
        compatibility_report.local_filesystem_alpha_compatible,
        report.durable_store_import_provider_compatibility_report_identity,
        "local filesystem alpha is not compatible"));

    // 检查 4: Capability Matrix Complete
    checks.push_back(run_check(
        "capability_matrix_complete",
        compatibility_report.capability_matrix_complete,
        report.durable_store_import_provider_compatibility_report_identity,
        "capability matrix is not complete"));

    // 检查 5: Registry Mock Adapter Registered
    checks.push_back(run_check(
        "registry_mock_adapter_registered",
        registry.mock_adapter_registered,
        report.durable_store_import_provider_registry_identity,
        "mock adapter is not registered in registry"));

    // 检查 6: Registry Local Filesystem Alpha Registered
    checks.push_back(run_check(
        "registry_local_filesystem_alpha_registered",
        registry.local_filesystem_alpha_registered,
        report.durable_store_import_provider_registry_identity,
        "local filesystem alpha is not registered in registry"));

    // 检查 7: Registry Compatibility Status Match
    checks.push_back(run_check(
        "registry_compatibility_status_match",
        registry.compatibility_status == ProviderCompatibilityStatus::Passed ||
            registry.compatibility_status == ProviderCompatibilityStatus::Failed,
        report.durable_store_import_provider_registry_identity,
        "registry compatibility status does not match expected values"));

    // 检查 8: Security Evidence Passed
    checks.push_back(run_check(
        "security_evidence_passed",
        readiness_evidence.security_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "security evidence did not pass"));

    // 检查 9: Recovery Evidence Passed
    checks.push_back(run_check(
        "recovery_evidence_passed",
        readiness_evidence.recovery_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "recovery evidence did not pass"));

    // 检查 10: Compatibility Evidence Passed
    checks.push_back(run_check(
        "compatibility_evidence_passed",
        readiness_evidence.compatibility_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "compatibility evidence did not pass"));

    // 检查 11: Observability Evidence Passed
    checks.push_back(run_check(
        "observability_evidence_passed",
        readiness_evidence.observability_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "observability evidence did not pass"));

    // 检查 12: Registry Evidence Passed
    checks.push_back(run_check(
        "registry_evidence_passed",
        readiness_evidence.registry_evidence_passed,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "registry evidence did not pass"));

    // 检查 13: No Blocking Issues
    checks.push_back(run_check(
        "no_blocking_issues",
        readiness_evidence.blocking_issue_count == 0,
        report.durable_store_import_provider_production_readiness_evidence_identity,
        "there are " + std::to_string(readiness_evidence.blocking_issue_count) +
            " blocking issues"));

    // 检查 14: Session ID Consistency
    checks.push_back(run_check(
        "session_id_consistency",
        compatibility_report.session_id == registry.session_id &&
            registry.session_id == readiness_evidence.session_id,
        report.durable_store_import_provider_conformance_report_identity,
        "session IDs are not consistent across artifacts"));

    // 检查 15: Workflow Name Consistency
    checks.push_back(run_check(
        "workflow_name_consistency",
        compatibility_report.workflow_canonical_name == registry.workflow_canonical_name &&
            registry.workflow_canonical_name == readiness_evidence.workflow_canonical_name,
        report.durable_store_import_provider_conformance_report_identity,
        "workflow canonical names are not consistent across artifacts"));

    // 检查 16: Input Fixture Consistency
    checks.push_back(run_check(
        "input_fixture_consistency",
        compatibility_report.input_fixture == registry.input_fixture &&
            registry.input_fixture == readiness_evidence.input_fixture,
        report.durable_store_import_provider_conformance_report_identity,
        "input fixtures are not consistent across artifacts"));

    // 计算统计数据
    int pass_count = 0;
    int fail_count = 0;
    int skipped_count = 0;
    for (const auto &check : checks) {
        switch (check.result) {
        case ConformanceCheckResult::Pass:
            ++pass_count;
            break;
        case ConformanceCheckResult::Fail:
            ++fail_count;
            break;
        case ConformanceCheckResult::Skipped:
            ++skipped_count;
            break;
        }
    }

    report.pass_count = pass_count;
    report.fail_count = fail_count;
    report.skipped_count = skipped_count;
    report.checks = std::move(checks);

    // 生成 conformance summary
    if (fail_count == 0) {
        report.conformance_summary = "provider conformance passed all checks";
    } else {
        report.conformance_summary = "provider conformance failed " +
                                     std::to_string(fail_count) + " of " +
                                     std::to_string(checks.size()) + " checks";
    }

    // 验证生成的 report
    result.diagnostics.append(validate_provider_conformance_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import

#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

namespace {

using namespace ahfl::durable_store_import;

// 辅助函数：创建有效的 conformance report
[[nodiscard]] ProviderConformanceReport make_conformance_report() {
    ProviderConformanceReport report;
    report.format_version = std::string(kProviderConformanceReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.input_fixture = "fixture.request.ok";
    report.durable_store_import_provider_compatibility_report_identity =
        "compatibility-report::session-001";
    report.durable_store_import_provider_registry_identity = "registry::session-001";
    report.durable_store_import_provider_production_readiness_evidence_identity =
        "readiness-evidence::session-001";
    report.durable_store_import_provider_conformance_report_identity =
        "conformance-report::session-001";
    report.pass_count = 16;
    report.fail_count = 0;
    report.skipped_count = 0;
    report.conformance_summary = "provider conformance passed all checks";
    return report;
}

// 辅助函数：创建有效的 schema compatibility report
[[nodiscard]] ProviderSchemaCompatibilityReport make_schema_report() {
    ProviderSchemaCompatibilityReport report;
    report.format_version = std::string(kProviderSchemaCompatibilityReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.has_schema_drift = false;
    report.compatibility_summary = "all versions compatible";
    return report;
}

// 辅助函数：创建有效的 config bundle validation report
[[nodiscard]] ProviderConfigBundleValidationReport make_config_report() {
    ProviderConfigBundleValidationReport report;
    report.format_version = std::string(kProviderConfigBundleValidationReportFormatVersion);
    report.source_durable_store_import_provider_selection_plan_format_version =
        std::string(kProviderSelectionPlanFormatVersion);
    report.source_durable_store_import_provider_config_snapshot_placeholder_format_version =
        std::string(kProviderConfigSnapshotPlaceholderFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.input_fixture = "fixture.request.ok";
    report.config_bundle_identity = "config-bundle::session-001";
    report.durable_store_import_provider_selection_plan_identity = "selection-plan::session-001";
    report.durable_store_import_provider_config_snapshot_identity = "config-snapshot::session-001";
    report.valid_count = 5;
    report.invalid_count = 0;
    report.missing_count = 0;
    report.validation_summary = "all validation checks passed";
    report.blocking_summary = "no blocking issues";
    report.reads_secret_value = false;
    report.opens_network_connection = false;
    report.generates_production_config = false;
    return report;
}

// 辅助函数：创建有效的 production readiness evidence
[[nodiscard]] ProviderProductionReadinessEvidence make_readiness_evidence() {
    ProviderProductionReadinessEvidence evidence;
    evidence.format_version = std::string(kProviderProductionReadinessEvidenceFormatVersion);
    evidence.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    evidence.session_id = "session-001";
    evidence.run_id = "run-001";
    evidence.input_fixture = "fixture.request.ok";
    evidence.durable_store_import_provider_selection_plan_identity = "selection-plan::session-001";
    evidence.durable_store_import_provider_compatibility_report_identity =
        "compatibility-report::session-001";
    evidence.durable_store_import_provider_execution_audit_event_identity =
        "audit-event::session-001";
    evidence.durable_store_import_provider_write_recovery_plan_identity =
        "recovery-plan::session-001";
    evidence.durable_store_import_provider_failure_taxonomy_report_identity =
        "taxonomy-report::session-001";
    evidence.durable_store_import_provider_production_readiness_evidence_identity =
        "readiness-evidence::session-001";
    evidence.security_evidence_passed = true;
    evidence.recovery_evidence_passed = true;
    evidence.compatibility_evidence_passed = true;
    evidence.observability_evidence_passed = true;
    evidence.registry_evidence_passed = true;
    evidence.blocking_issue_count = 0;
    evidence.evidence_summary = "all readiness checks passed";
    return evidence;
}

// 测试：构建 release evidence archive manifest 成功
int test_build_release_evidence_archive_manifest() {
    const auto conformance = make_conformance_report();
    const auto schema = make_schema_report();
    const auto config = make_config_report();
    const auto readiness = make_readiness_evidence();

    const auto result = build_release_evidence_archive_manifest(conformance, schema, config, readiness);

    if (!result.manifest.has_value()) {
        std::cerr << "FAIL: build_release_evidence_archive_manifest should produce a manifest\n";
        return 1;
    }

    const auto &manifest = *result.manifest;

    // 验证身份字段
    assert(manifest.workflow_canonical_name == "app::main::ValueFlowWorkflow");
    assert(manifest.session_id == "session-001");
    assert(manifest.run_id.has_value() && *manifest.run_id == "run-001");

    // 验证 evidence items 数量
    assert(manifest.total_evidence_count == 4);
    assert(manifest.evidence_items.size() == 4);

    // 验证统计计数
    assert(manifest.present_evidence_count == 4);
    assert(manifest.valid_evidence_count == 4);
    assert(manifest.missing_evidence_count == 0);
    assert(manifest.invalid_evidence_count == 0);

    // 验证 release ready 状态
    assert(manifest.is_release_ready);

    // 验证汇总字段不为空
    assert(!manifest.archive_summary.empty());
    assert(!manifest.missing_evidence_summary.empty());
    assert(!manifest.incompatible_evidence_summary.empty());

    std::cout << "PASS: test_build_release_evidence_archive_manifest\n";
    return 0;
}

// 测试：无效 format_version 应被检测
int test_validate_manifest_format_version() {
    ReleaseEvidenceArchiveManifest manifest;
    manifest.format_version = "invalid-version";
    manifest.workflow_canonical_name = "test-workflow";
    manifest.session_id = "session-001";
    manifest.durable_store_import_provider_conformance_report_identity = "conformance-001";
    manifest.durable_store_import_provider_schema_compatibility_report_identity = "schema-001";
    manifest.durable_store_import_provider_config_bundle_validation_report_identity = "config-001";
    manifest.durable_store_import_provider_production_readiness_evidence_identity = "readiness-001";
    manifest.archive_summary = "test summary";
    manifest.total_evidence_count = 0;

    const auto result = validate_release_evidence_archive_manifest(manifest);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_manifest_format_version\n";
    return 0;
}

// 测试：空字段应被检测
int test_validate_manifest_empty_fields() {
    ReleaseEvidenceArchiveManifest manifest;
    manifest.format_version = std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion);
    manifest.workflow_canonical_name = "";  // 空
    manifest.session_id = "";  // 空
    manifest.archive_summary = "test summary";

    const auto result = validate_release_evidence_archive_manifest(manifest);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect empty required fields\n";
        return 1;
    }

    std::cout << "PASS: test_validate_manifest_empty_fields\n";
    return 0;
}

// 测试：has_errors 的 conformance report 导致 manifest 构建失败
int test_build_with_invalid_conformance() {
    ProviderConformanceReport conformance;
    conformance.format_version = "invalid";  // 无效版本

    const auto schema = make_schema_report();
    const auto config = make_config_report();
    const auto readiness = make_readiness_evidence();

    const auto result =
        build_release_evidence_archive_manifest(conformance, schema, config, readiness);

    if (result.manifest.has_value()) {
        std::cerr << "FAIL: invalid conformance report should prevent manifest creation\n";
        return 1;
    }

    std::cout << "PASS: test_build_with_invalid_conformance\n";
    return 0;
}

// 测试：失败 conformance 检查导致 invalid evidence
int test_invalid_conformance_evidence() {
    auto conformance = make_conformance_report();
    conformance.fail_count = 2;  // 有失败的检查
    conformance.conformance_summary = "failed 2 checks";

    const auto schema = make_schema_report();
    const auto config = make_config_report();
    const auto readiness = make_readiness_evidence();

    const auto result =
        build_release_evidence_archive_manifest(conformance, schema, config, readiness);

    if (!result.manifest.has_value()) {
        std::cerr << "FAIL: should produce manifest even with failed conformance\n";
        return 1;
    }

    const auto &manifest = *result.manifest;

    // 验证有 invalid evidence
    assert(manifest.invalid_evidence_count >= 1);
    assert(!manifest.is_release_ready);

    std::cout << "PASS: test_invalid_conformance_evidence\n";
    return 0;
}

// 测试：计数一致性
int test_count_consistency() {
    const auto conformance = make_conformance_report();
    const auto schema = make_schema_report();
    const auto config = make_config_report();
    const auto readiness = make_readiness_evidence();

    const auto result =
        build_release_evidence_archive_manifest(conformance, schema, config, readiness);

    const auto &manifest = *result.manifest;

    // 验证计数一致性
    assert(manifest.total_evidence_count ==
           manifest.present_evidence_count + manifest.missing_evidence_count);
    assert(manifest.present_evidence_count ==
           manifest.valid_evidence_count + manifest.invalid_evidence_count);

    std::cout << "PASS: test_count_consistency\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // 支持通过命令行参数选择单个测试用例（CTest 集成）
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-manifest") return test_build_release_evidence_archive_manifest();
        if (cmd == "test-validate-format-version") return test_validate_manifest_format_version();
        if (cmd == "test-validate-empty-fields") return test_validate_manifest_empty_fields();
        if (cmd == "test-build-with-invalid-conformance")
            return test_build_with_invalid_conformance();
        if (cmd == "test-invalid-conformance-evidence") return test_invalid_conformance_evidence();
        if (cmd == "test-count-consistency") return test_count_consistency();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    // 无参数时运行全部测试
    int failures = 0;
    failures += test_build_release_evidence_archive_manifest();
    failures += test_validate_manifest_format_version();
    failures += test_validate_manifest_empty_fields();
    failures += test_build_with_invalid_conformance();
    failures += test_invalid_conformance_evidence();
    failures += test_count_consistency();

    if (failures == 0) {
        std::cout << "\nAll release evidence archive tests passed.\n";
    }
    return failures;
}

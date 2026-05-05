#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

namespace {

using namespace ahfl::durable_store_import;

[[nodiscard]] ProviderSelectionPlan make_selection_plan() {
    ProviderSelectionPlan plan;
    plan.format_version = std::string(kProviderSelectionPlanFormatVersion);
    plan.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    plan.session_id = "session-001";
    plan.run_id = "run-001";
    plan.input_fixture = "fixture.request.ok";
    plan.selected_provider_id = "local-filesystem-alpha";
    plan.fallback_provider_id = "sdk-mock-adapter";
    plan.selection_status = ProviderSelectionStatus::Selected;
    plan.durable_store_import_provider_selection_plan_identity =
        "selection-plan::app::main::ValueFlowWorkflow::session-001";
    return plan;
}

[[nodiscard]] ProviderConfigSnapshotPlaceholder make_config_snapshot() {
    ProviderConfigSnapshotPlaceholder snapshot;
    snapshot.format_version = std::string(kProviderConfigSnapshotPlaceholderFormatVersion);
    snapshot.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    snapshot.session_id = "session-001";
    snapshot.run_id = "run-001";
    snapshot.input_fixture = "fixture.request.ok";
    snapshot.durable_store_import_provider_config_snapshot_identity =
        "config-snapshot::app::main::ValueFlowWorkflow::session-001";
    snapshot.provider_config_profile_identity = "local-placeholder-profile";
    snapshot.snapshot_status = ProviderConfigStatus::Ready;
    snapshot.reads_secret_material = true;
    snapshot.materializes_secret_value = false;
    return snapshot;
}

// 测试：构建校验报告成功且安全约束满足
int test_build_validation_report() {
    const auto plan = make_selection_plan();
    const auto snapshot = make_config_snapshot();

    const auto result = build_provider_config_bundle_validation_report(plan, snapshot);

    if (!result.report.has_value()) {
        std::cerr << "FAIL: build_provider_config_bundle_validation_report should produce a report\n";
        return 1;
    }

    const auto &report = *result.report;

    // 验证身份字段
    assert(report.workflow_canonical_name == "app::main::ValueFlowWorkflow");
    assert(report.session_id == "session-001");

    // 验证 provider references
    assert(report.provider_references.size() == 2);

    // 验证安全约束
    assert(!report.reads_secret_value);
    assert(!report.opens_network_connection);
    assert(!report.generates_production_config);

    std::cout << "PASS: test_build_validation_report\n";
    return 0;
}

// 测试：无效 format_version 应被检测
int test_validate_report_format_version() {
    ProviderConfigBundleValidationReport report;
    report.format_version = "invalid-version";
    report.workflow_canonical_name = "test-workflow";
    report.session_id = "session-001";
    report.input_fixture = "fixture";
    report.config_bundle_identity = "config-bundle-001";
    report.durable_store_import_provider_selection_plan_identity = "selection-plan-001";
    report.durable_store_import_provider_config_snapshot_identity = "config-snapshot-001";
    report.validation_summary = "summary";
    report.blocking_summary = "blocking";

    const auto result = validate_provider_config_bundle_validation_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_report_format_version\n";
    return 0;
}

// 测试：reads_secret_value=true 应违反安全约束
int test_validate_report_security_constraints() {
    ProviderConfigBundleValidationReport report;
    report.format_version = std::string(kProviderConfigBundleValidationReportFormatVersion);
    report.source_durable_store_import_provider_selection_plan_format_version =
        std::string(kProviderSelectionPlanFormatVersion);
    report.source_durable_store_import_provider_config_snapshot_placeholder_format_version =
        std::string(kProviderConfigSnapshotPlaceholderFormatVersion);
    report.workflow_canonical_name = "test-workflow";
    report.session_id = "session-001";
    report.input_fixture = "fixture";
    report.config_bundle_identity = "config-bundle-001";
    report.durable_store_import_provider_selection_plan_identity = "selection-plan-001";
    report.durable_store_import_provider_config_snapshot_identity = "config-snapshot-001";
    report.validation_summary = "summary";
    report.blocking_summary = "blocking";
    report.reads_secret_value = true;  // 违反安全约束

    const auto result = validate_provider_config_bundle_validation_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect reads_secret_value violation\n";
        return 1;
    }

    std::cout << "PASS: test_validate_report_security_constraints\n";
    return 0;
}

// 测试：构建报告后汇总计数合理
int test_summary_counts() {
    const auto plan = make_selection_plan();
    const auto snapshot = make_config_snapshot();

    const auto result = build_provider_config_bundle_validation_report(plan, snapshot);

    if (!result.report.has_value()) {
        std::cerr << "FAIL: report should have value\n";
        return 1;
    }

    const auto &report = *result.report;

    // 检查汇总字段不为空
    assert(!report.validation_summary.empty());
    assert(!report.blocking_summary.empty());

    // 检查计数一致性
    const int total = report.valid_count + report.invalid_count + report.missing_count;
    assert(total > 0);

    std::cout << "PASS: test_summary_counts\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // 支持通过命令行参数选择单个测试用例（CTest 集成）
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-validation-report") return test_build_validation_report();
        if (cmd == "test-validate-format-version") return test_validate_report_format_version();
        if (cmd == "test-validate-security-constraints") return test_validate_report_security_constraints();
        if (cmd == "test-summary-counts") return test_summary_counts();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    // 无参数时运行全部测试
    int failures = 0;
    failures += test_build_validation_report();
    failures += test_validate_report_format_version();
    failures += test_validate_report_security_constraints();
    failures += test_summary_counts();

    if (failures == 0) {
        std::cout << "\nAll config bundle validation tests passed.\n";
    }
    return failures;
}

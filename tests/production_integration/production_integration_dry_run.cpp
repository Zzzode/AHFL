#include "ahfl/durable_store_import/provider_production_integration_dry_run.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::durable_store_import;

// 辅助函数：创建通过的 ConformanceReport
[[nodiscard]] ProviderConformanceReport make_passing_conformance() {
    ProviderConformanceReport report;
    report.format_version = std::string(kProviderConformanceReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.durable_store_import_provider_conformance_report_identity =
        "conformance-report::session-001";
    report.pass_count = 5;
    report.fail_count = 0;
    report.skipped_count = 0;
    report.conformance_summary = "all checks passed";
    return report;
}

// 辅助函数：创建通过的 SchemaCompatibilityReport
[[nodiscard]] ProviderSchemaCompatibilityReport make_compatible_schema() {
    ProviderSchemaCompatibilityReport report;
    report.format_version = std::string(kProviderSchemaCompatibilityReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.compatible_count = 7;
    report.incompatible_count = 0;
    report.unknown_count = 0;
    report.has_schema_drift = false;
    report.compatibility_summary = "all versions compatible";
    return report;
}

// 辅助函数：创建通过的 ConfigBundleValidationReport
[[nodiscard]] ProviderConfigBundleValidationReport make_valid_config() {
    ProviderConfigBundleValidationReport report;
    report.format_version = std::string(kProviderConfigBundleValidationReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.config_bundle_identity = "config-bundle::session-001";
    report.valid_count = 5;
    report.invalid_count = 0;
    report.missing_count = 0;
    report.reads_secret_value = false;
    report.opens_network_connection = false;
    report.validation_summary = "all entries valid";
    return report;
}

// 辅助函数：创建通过的 ReleaseEvidenceArchiveManifest
[[nodiscard]] ReleaseEvidenceArchiveManifest make_release_ready_archive() {
    ReleaseEvidenceArchiveManifest manifest;
    manifest.format_version = std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion);
    manifest.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    manifest.session_id = "session-001";
    manifest.durable_store_import_provider_conformance_report_identity =
        "conformance-report::session-001";
    manifest.total_evidence_count = 4;
    manifest.present_evidence_count = 4;
    manifest.valid_evidence_count = 4;
    manifest.missing_evidence_count = 0;
    manifest.invalid_evidence_count = 0;
    manifest.is_release_ready = true;
    manifest.archive_summary = "all evidence present and valid";
    return manifest;
}

// 辅助函数：创建通过的 ApprovalReceipt
[[nodiscard]] ApprovalReceipt make_approved_receipt() {
    ApprovalReceipt receipt;
    receipt.format_version = std::string(kProviderApprovalReceiptFormatVersion);
    receipt.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    receipt.session_id = "session-001";
    receipt.run_id = "run-001";
    receipt.approval_request_identity = "approval-request::session-001";
    receipt.approval_decision_identity = "approval-decision::session-001";
    receipt.final_decision = ApprovalDecision::Approved;
    receipt.is_approved = true;
    receipt.receipt_summary = "approved by operator";
    return receipt;
}

// 辅助函数：创建允许的 OptInDecisionReport
[[nodiscard]] ProviderOptInDecisionReport make_allow_opt_in() {
    ProviderOptInDecisionReport report;
    report.format_version = std::string(kProviderOptInDecisionReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.decision = OptInDecision::Allow;
    report.is_real_provider_traffic_allowed = true;
    report.gates_passed = 4;
    report.gates_failed = 0;
    return report;
}

// 辅助函数：创建允许执行的 RuntimePolicyReport
[[nodiscard]] ProviderRuntimePolicyReport make_permitted_policy() {
    ProviderRuntimePolicyReport report;
    report.format_version = std::string(kProviderRuntimePolicyReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.decision = PolicyDecision::Permit;
    report.is_execution_permitted = true;
    report.gates_passed = 5;
    report.gates_failed = 0;
    return report;
}

// 测试：所有 evidence 通过时，ReadyForControlledRollout
int test_build_all_evidence_pass() {
    const auto result = build_provider_production_integration_dry_run_report(
        make_passing_conformance(), make_compatible_schema(), make_valid_config(),
        make_release_ready_archive(), make_approved_receipt(), make_allow_opt_in(),
        make_permitted_policy());

    if (!result.report.has_value()) {
        std::cerr << "FAIL: build should produce a report\n";
        return 1;
    }

    const auto &report = *result.report;

    assert(report.readiness_state == ProductionReadinessState::ReadyForControlledRollout);
    assert(report.is_ready_for_controlled_rollout == true);
    assert(report.is_non_mutating_mode == true);
    assert(report.valid_evidence_count == 7);
    assert(report.invalid_evidence_count == 0);
    assert(report.missing_evidence_count == 0);
    assert(report.blocking_item_count == 0);

    std::cout << "PASS: test_build_all_evidence_pass\n";
    return 0;
}

// 测试：conformance 失败时 Blocked
int test_build_blocked_conformance_fails() {
    auto conformance = make_passing_conformance();
    conformance.fail_count = 2;

    const auto result = build_provider_production_integration_dry_run_report(
        conformance, make_compatible_schema(), make_valid_config(),
        make_release_ready_archive(), make_approved_receipt(), make_allow_opt_in(),
        make_permitted_policy());
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.readiness_state == ProductionReadinessState::Blocked);
    assert(report.is_ready_for_controlled_rollout == false);
    assert(report.invalid_evidence_count > 0);
    assert(report.blocking_item_count > 0);

    std::cout << "PASS: test_build_blocked_conformance_fails\n";
    return 0;
}

// 测试：schema 不兼容时 Blocked
int test_build_blocked_schema_incompatible() {
    auto schema = make_compatible_schema();
    schema.incompatible_count = 1;

    const auto result = build_provider_production_integration_dry_run_report(
        make_passing_conformance(), schema, make_valid_config(),
        make_release_ready_archive(), make_approved_receipt(), make_allow_opt_in(),
        make_permitted_policy());
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.readiness_state == ProductionReadinessState::Blocked);
    assert(report.is_ready_for_controlled_rollout == false);

    std::cout << "PASS: test_build_blocked_schema_incompatible\n";
    return 0;
}

// 测试：approval 被拒绝时 Blocked
int test_build_blocked_approval_rejected() {
    auto receipt = make_approved_receipt();
    receipt.is_approved = false;
    receipt.final_decision = ApprovalDecision::Rejected;

    const auto result = build_provider_production_integration_dry_run_report(
        make_passing_conformance(), make_compatible_schema(), make_valid_config(),
        make_release_ready_archive(), receipt, make_allow_opt_in(), make_permitted_policy());
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.readiness_state == ProductionReadinessState::Blocked);
    assert(report.is_ready_for_controlled_rollout == false);

    std::cout << "PASS: test_build_blocked_approval_rejected\n";
    return 0;
}

// 测试：runtime policy deny 时 Blocked
int test_build_blocked_runtime_policy_deny() {
    auto policy = make_permitted_policy();
    policy.decision = PolicyDecision::Deny;
    policy.is_execution_permitted = false;

    const auto result = build_provider_production_integration_dry_run_report(
        make_passing_conformance(), make_compatible_schema(), make_valid_config(),
        make_release_ready_archive(), make_approved_receipt(), make_allow_opt_in(), policy);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.readiness_state == ProductionReadinessState::Blocked);
    assert(report.is_ready_for_controlled_rollout == false);

    std::cout << "PASS: test_build_blocked_runtime_policy_deny\n";
    return 0;
}

// 测试：验证 format_version 校验
int test_validate_format_version() {
    ProviderProductionIntegrationDryRunReport report;
    report.format_version = "invalid-version";
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";

    const auto result = validate_provider_production_integration_dry_run_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format_version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_format_version\n";
    return 0;
}

// 测试：验证 readiness/is_ready 一致性
int test_validate_readiness_consistency() {
    ProviderProductionIntegrationDryRunReport report;
    report.format_version =
        std::string(kProviderProductionIntegrationDryRunReportFormatVersion);
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.readiness_state = ProductionReadinessState::Blocked;
    report.is_ready_for_controlled_rollout = true;  // 不一致

    const auto result = validate_provider_production_integration_dry_run_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect readiness/is_ready inconsistency\n";
        return 1;
    }

    std::cout << "PASS: test_validate_readiness_consistency\n";
    return 0;
}

// 测试：默认值确保安全（is_ready=false, is_non_mutating=true, state=Blocked）
int test_default_safe_values() {
    ProviderProductionIntegrationDryRunReport report;

    assert(report.is_ready_for_controlled_rollout == false);
    assert(report.is_non_mutating_mode == true);
    assert(report.readiness_state == ProductionReadinessState::Blocked);

    std::cout << "PASS: test_default_safe_values\n";
    return 0;
}

// 测试：is_non_mutating_mode 校验
int test_validate_non_mutating_mode() {
    ProviderProductionIntegrationDryRunReport report;
    report.format_version =
        std::string(kProviderProductionIntegrationDryRunReportFormatVersion);
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.is_non_mutating_mode = false;  // 当前版本不允许

    const auto result = validate_provider_production_integration_dry_run_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should reject is_non_mutating_mode=false\n";
        return 1;
    }

    std::cout << "PASS: test_validate_non_mutating_mode\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-all-evidence-pass") return test_build_all_evidence_pass();
        if (cmd == "test-build-blocked-conformance-fails")
            return test_build_blocked_conformance_fails();
        if (cmd == "test-build-blocked-schema-incompatible")
            return test_build_blocked_schema_incompatible();
        if (cmd == "test-build-blocked-approval-rejected")
            return test_build_blocked_approval_rejected();
        if (cmd == "test-build-blocked-runtime-policy-deny")
            return test_build_blocked_runtime_policy_deny();
        if (cmd == "test-validate-format-version") return test_validate_format_version();
        if (cmd == "test-validate-readiness-consistency")
            return test_validate_readiness_consistency();
        if (cmd == "test-default-safe-values") return test_default_safe_values();
        if (cmd == "test-validate-non-mutating-mode") return test_validate_non_mutating_mode();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    int failures = 0;
    failures += test_build_all_evidence_pass();
    failures += test_build_blocked_conformance_fails();
    failures += test_build_blocked_schema_incompatible();
    failures += test_build_blocked_approval_rejected();
    failures += test_build_blocked_runtime_policy_deny();
    failures += test_validate_format_version();
    failures += test_validate_readiness_consistency();
    failures += test_default_safe_values();
    failures += test_validate_non_mutating_mode();

    if (failures == 0) {
        std::cout << "\nAll production integration dry run tests passed.\n";
    }
    return failures;
}

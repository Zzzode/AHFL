#include "ahfl/durable_store_import/provider_runtime_policy.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::durable_store_import;

// 辅助函数：创建通过审批的 ApprovalReceipt
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
    receipt.blocking_reason_summary = "none";
    return receipt;
}

// 辅助函数：创建拒绝审批的 ApprovalReceipt
[[nodiscard]] ApprovalReceipt make_rejected_receipt() {
    ApprovalReceipt receipt;
    receipt.format_version = std::string(kProviderApprovalReceiptFormatVersion);
    receipt.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    receipt.session_id = "session-001";
    receipt.run_id = "run-001";
    receipt.approval_request_identity = "approval-request::session-001";
    receipt.approval_decision_identity = "approval-decision::session-001";
    receipt.final_decision = ApprovalDecision::Rejected;
    receipt.is_approved = false;
    receipt.receipt_summary = "rejected by operator";
    receipt.blocking_reason_summary = "not approved";
    return receipt;
}

// 辅助函数：创建有效的 ProviderConfigBundleValidationReport
[[nodiscard]] ProviderConfigBundleValidationReport make_valid_config_report() {
    ProviderConfigBundleValidationReport report;
    report.format_version =
        std::string(kProviderConfigBundleValidationReportFormatVersion);
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

// 辅助函数：创建有效选中的 ProviderSelectionPlan
[[nodiscard]] ProviderSelectionPlan make_selected_plan() {
    ProviderSelectionPlan plan;
    plan.format_version = std::string(kProviderSelectionPlanFormatVersion);
    plan.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    plan.session_id = "session-001";
    plan.durable_store_import_provider_selection_plan_identity =
        "selection-plan::session-001";
    plan.selection_status = ProviderSelectionStatus::Selected;
    plan.selected_provider_id = "provider-local-fs-alpha";
    plan.capability_gap_summary = "none";
    return plan;
}

// 辅助函数：创建通过就绪检查的 readiness evidence
[[nodiscard]] ProviderProductionReadinessEvidence make_passing_readiness() {
    ProviderProductionReadinessEvidence evidence;
    evidence.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    evidence.session_id = "session-001";
    evidence.durable_store_import_provider_production_readiness_evidence_identity =
        "readiness-evidence::session-001";
    evidence.security_evidence_passed = true;
    evidence.recovery_evidence_passed = true;
    evidence.compatibility_evidence_passed = true;
    evidence.observability_evidence_passed = true;
    evidence.registry_evidence_passed = true;
    evidence.blocking_issue_count = 0;
    evidence.evidence_summary = "all evidence passed";
    return evidence;
}

// 辅助函数：创建允许 traffic 的 opt-in decision report
[[nodiscard]] ProviderOptInDecisionReport make_allow_opt_in_report() {
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

// 辅助函数：创建拒绝 traffic 的 opt-in decision report
[[nodiscard]] ProviderOptInDecisionReport make_deny_opt_in_report() {
    ProviderOptInDecisionReport report;
    report.format_version = std::string(kProviderOptInDecisionReportFormatVersion);
    report.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    report.session_id = "session-001";
    report.run_id = "run-001";
    report.decision = OptInDecision::Deny;
    report.is_real_provider_traffic_allowed = false;
    report.gates_passed = 0;
    report.gates_failed = 4;
    report.denial_reasons.push_back(OptInDenialReason::NoApproval);
    return report;
}

// 测试：所有 gate 通过时 Permit
int test_build_all_gates_pass() {
    const auto opt_in = make_allow_opt_in_report();
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();
    const auto readiness = make_passing_readiness();

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);

    if (!result.report.has_value()) {
        std::cerr << "FAIL: build should produce a report\n";
        return 1;
    }

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Permit);
    assert(report.is_execution_permitted == true);
    assert(report.gates_passed == 5);
    assert(report.gates_failed == 0);
    assert(report.blocking_violation_count == 0);

    std::cout << "PASS: test_build_all_gates_pass\n";
    return 0;
}

// 测试：opt-in 未授予时 Deny
int test_build_deny_opt_in_not_granted() {
    const auto opt_in = make_deny_opt_in_report();
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();
    const auto readiness = make_passing_readiness();

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Deny);
    assert(report.is_execution_permitted == false);
    assert(report.gates_failed > 0);
    assert(report.blocking_violation_count > 0);

    // 确认 opt_in gate 失败
    bool found_opt_in_failure = false;
    for (const auto &gate : report.policy_gates) {
        if (gate.gate_name == "opt_in" && !gate.passed) {
            found_opt_in_failure = true;
            bool has_violation = false;
            for (const auto &v : gate.violations) {
                if (v == PolicyViolationType::OptInNotGranted) {
                    has_violation = true;
                }
            }
            assert(has_violation);
        }
    }
    assert(found_opt_in_failure);

    std::cout << "PASS: test_build_deny_opt_in_not_granted\n";
    return 0;
}

// 测试：审批缺失时 Deny
int test_build_deny_approval_missing() {
    const auto opt_in = make_allow_opt_in_report();
    const auto receipt = make_rejected_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();
    const auto readiness = make_passing_readiness();

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Deny);
    assert(report.is_execution_permitted == false);

    bool found_approval_failure = false;
    for (const auto &gate : report.policy_gates) {
        if (gate.gate_name == "approval" && !gate.passed) {
            found_approval_failure = true;
        }
    }
    assert(found_approval_failure);

    std::cout << "PASS: test_build_deny_approval_missing\n";
    return 0;
}

// 测试：config 无效时 Deny
int test_build_deny_config_invalid() {
    const auto opt_in = make_allow_opt_in_report();
    const auto receipt = make_approved_receipt();
    auto config = make_valid_config_report();
    config.invalid_count = 3;
    const auto plan = make_selected_plan();
    const auto readiness = make_passing_readiness();

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Deny);
    assert(report.is_execution_permitted == false);

    bool found_config_failure = false;
    for (const auto &gate : report.policy_gates) {
        if (gate.gate_name == "config" && !gate.passed) {
            found_config_failure = true;
        }
    }
    assert(found_config_failure);

    std::cout << "PASS: test_build_deny_config_invalid\n";
    return 0;
}

// 测试：registry 不匹配时 Deny
int test_build_deny_registry_mismatch() {
    const auto opt_in = make_allow_opt_in_report();
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    auto plan = make_selected_plan();
    plan.selection_status = ProviderSelectionStatus::Blocked;
    const auto readiness = make_passing_readiness();

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Deny);
    assert(report.is_execution_permitted == false);

    bool found_registry_failure = false;
    for (const auto &gate : report.policy_gates) {
        if (gate.gate_name == "registry" && !gate.passed) {
            found_registry_failure = true;
        }
    }
    assert(found_registry_failure);

    std::cout << "PASS: test_build_deny_registry_mismatch\n";
    return 0;
}

// 测试：readiness 未通过时 Deny
int test_build_deny_readiness_not_met() {
    const auto opt_in = make_allow_opt_in_report();
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();
    auto readiness = make_passing_readiness();
    readiness.blocking_issue_count = 2;

    const auto result = build_provider_runtime_policy_report(
        opt_in, receipt, config, plan, readiness);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == PolicyDecision::Deny);
    assert(report.is_execution_permitted == false);

    bool found_readiness_failure = false;
    for (const auto &gate : report.policy_gates) {
        if (gate.gate_name == "readiness" && !gate.passed) {
            found_readiness_failure = true;
        }
    }
    assert(found_readiness_failure);

    std::cout << "PASS: test_build_deny_readiness_not_met\n";
    return 0;
}

// 测试：验证 format_version 校验
int test_validate_format_version() {
    ProviderRuntimePolicyReport report;
    report.format_version = "invalid-version";
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.decision = PolicyDecision::Deny;
    report.is_execution_permitted = false;

    const auto result = validate_provider_runtime_policy_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format_version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_format_version\n";
    return 0;
}

// 测试：验证 decision/is_execution_permitted 一致性
int test_validate_decision_consistency() {
    ProviderRuntimePolicyReport report;
    report.format_version = std::string(kProviderRuntimePolicyReportFormatVersion);
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.decision = PolicyDecision::Deny;
    report.is_execution_permitted = true;  // 不一致

    const auto result = validate_provider_runtime_policy_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect decision/permit inconsistency\n";
        return 1;
    }

    std::cout << "PASS: test_validate_decision_consistency\n";
    return 0;
}

// 测试：默认值确保 is_execution_permitted 为 false、decision 为 Deny
int test_default_deny() {
    ProviderRuntimePolicyReport report;

    // 核心安全约束：默认值必须为 Deny + false
    assert(report.is_execution_permitted == false);
    assert(report.decision == PolicyDecision::Deny);

    std::cout << "PASS: test_default_deny\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-all-gates-pass") return test_build_all_gates_pass();
        if (cmd == "test-build-deny-opt-in-not-granted")
            return test_build_deny_opt_in_not_granted();
        if (cmd == "test-build-deny-approval-missing")
            return test_build_deny_approval_missing();
        if (cmd == "test-build-deny-config-invalid") return test_build_deny_config_invalid();
        if (cmd == "test-build-deny-registry-mismatch")
            return test_build_deny_registry_mismatch();
        if (cmd == "test-build-deny-readiness-not-met")
            return test_build_deny_readiness_not_met();
        if (cmd == "test-validate-format-version") return test_validate_format_version();
        if (cmd == "test-validate-decision-consistency")
            return test_validate_decision_consistency();
        if (cmd == "test-default-deny") return test_default_deny();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    int failures = 0;
    failures += test_build_all_gates_pass();
    failures += test_build_deny_opt_in_not_granted();
    failures += test_build_deny_approval_missing();
    failures += test_build_deny_config_invalid();
    failures += test_build_deny_registry_mismatch();
    failures += test_build_deny_readiness_not_met();
    failures += test_validate_format_version();
    failures += test_validate_decision_consistency();
    failures += test_default_deny();

    if (failures == 0) {
        std::cout << "\nAll runtime policy tests passed.\n";
    }
    return failures;
}

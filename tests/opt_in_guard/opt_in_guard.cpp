#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"

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

// 测试：所有 gate 通过时允许 traffic
int test_build_all_gates_pass() {
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();

    const auto result = build_provider_opt_in_decision_report(receipt, config, plan);

    if (!result.report.has_value()) {
        std::cerr << "FAIL: build should produce a report\n";
        return 1;
    }

    const auto &report = *result.report;

    assert(report.decision == OptInDecision::Allow);
    assert(report.is_real_provider_traffic_allowed == true);
    assert(report.gates_passed == 4);
    assert(report.gates_failed == 0);
    assert(report.denial_reasons.empty());
    assert(report.denial_reason_summary == "none");

    std::cout << "PASS: test_build_all_gates_pass\n";
    return 0;
}

// 测试：未审批时默认拒绝 traffic
int test_build_deny_no_approval() {
    const auto receipt = make_rejected_receipt();
    const auto config = make_valid_config_report();
    const auto plan = make_selected_plan();

    const auto result = build_provider_opt_in_decision_report(receipt, config, plan);

    if (!result.report.has_value()) {
        std::cerr << "FAIL: build should produce a report\n";
        return 1;
    }

    const auto &report = *result.report;

    // 核心约束：默认拒绝
    assert(report.decision == OptInDecision::Deny);
    assert(report.is_real_provider_traffic_allowed == false);
    assert(report.gates_failed > 0);
    assert(!report.denial_reasons.empty());

    // 检查包含 NoApproval 拒绝原因
    bool has_no_approval = false;
    for (const auto &reason : report.denial_reasons) {
        if (reason == OptInDenialReason::NoApproval) {
            has_no_approval = true;
        }
    }
    assert(has_no_approval);

    std::cout << "PASS: test_build_deny_no_approval\n";
    return 0;
}

// 测试：config 无效时拒绝
int test_build_deny_config_invalid() {
    const auto receipt = make_approved_receipt();
    auto config = make_valid_config_report();
    config.invalid_count = 2;  // 模拟配置无效
    const auto plan = make_selected_plan();

    const auto result = build_provider_opt_in_decision_report(receipt, config, plan);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == OptInDecision::Deny);
    assert(report.is_real_provider_traffic_allowed == false);

    bool has_config_invalid = false;
    for (const auto &reason : report.denial_reasons) {
        if (reason == OptInDenialReason::ConfigInvalid) {
            has_config_invalid = true;
        }
    }
    assert(has_config_invalid);

    std::cout << "PASS: test_build_deny_config_invalid\n";
    return 0;
}

// 测试：registry 不匹配时拒绝
int test_build_deny_registry_mismatch() {
    const auto receipt = make_approved_receipt();
    const auto config = make_valid_config_report();
    auto plan = make_selected_plan();
    plan.selection_status = ProviderSelectionStatus::Blocked;

    const auto result = build_provider_opt_in_decision_report(receipt, config, plan);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == OptInDecision::Deny);
    assert(report.is_real_provider_traffic_allowed == false);

    bool has_registry_mismatch = false;
    for (const auto &reason : report.denial_reasons) {
        if (reason == OptInDenialReason::RegistryMismatch) {
            has_registry_mismatch = true;
        }
    }
    assert(has_registry_mismatch);

    std::cout << "PASS: test_build_deny_registry_mismatch\n";
    return 0;
}

// 测试：安全约束（reads_secret_value）导致拒绝
int test_build_deny_security_constraints() {
    const auto receipt = make_approved_receipt();
    auto config = make_valid_config_report();
    config.reads_secret_value = true;  // 违反安全约束
    const auto plan = make_selected_plan();

    const auto result = build_provider_opt_in_decision_report(receipt, config, plan);
    assert(result.report.has_value());

    const auto &report = *result.report;

    assert(report.decision == OptInDecision::Deny);
    assert(report.is_real_provider_traffic_allowed == false);

    bool has_readiness_not_met = false;
    for (const auto &reason : report.denial_reasons) {
        if (reason == OptInDenialReason::ReadinessNotMet) {
            has_readiness_not_met = true;
        }
    }
    assert(has_readiness_not_met);

    std::cout << "PASS: test_build_deny_security_constraints\n";
    return 0;
}

// 测试：验证 format_version 校验
int test_validate_format_version() {
    ProviderOptInDecisionReport report;
    report.format_version = "invalid-version";
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.decision = OptInDecision::Deny;
    report.is_real_provider_traffic_allowed = false;
    report.denial_reasons.push_back(OptInDenialReason::ExplicitDeny);

    const auto result = validate_provider_opt_in_decision_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format_version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_format_version\n";
    return 0;
}

// 测试：验证 decision/is_real_provider_traffic_allowed 一致性
int test_validate_decision_consistency() {
    ProviderOptInDecisionReport report;
    report.format_version = std::string(kProviderOptInDecisionReportFormatVersion);
    report.workflow_canonical_name = "test";
    report.session_id = "session-001";
    report.decision = OptInDecision::Deny;
    report.is_real_provider_traffic_allowed = true;  // 不一致
    report.denial_reasons.push_back(OptInDenialReason::ExplicitDeny);

    const auto result = validate_provider_opt_in_decision_report(report);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect decision/traffic inconsistency\n";
        return 1;
    }

    std::cout << "PASS: test_validate_decision_consistency\n";
    return 0;
}

// 测试：默认值确保 is_real_provider_traffic_allowed 为 false
int test_default_deny() {
    ProviderOptInDecisionReport report;

    // 核心安全约束：默认值必须为 false
    assert(report.is_real_provider_traffic_allowed == false);
    assert(report.decision == OptInDecision::Deny);

    std::cout << "PASS: test_default_deny\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-all-gates-pass") return test_build_all_gates_pass();
        if (cmd == "test-build-deny-no-approval") return test_build_deny_no_approval();
        if (cmd == "test-build-deny-config-invalid") return test_build_deny_config_invalid();
        if (cmd == "test-build-deny-registry-mismatch") return test_build_deny_registry_mismatch();
        if (cmd == "test-build-deny-security-constraints")
            return test_build_deny_security_constraints();
        if (cmd == "test-validate-format-version") return test_validate_format_version();
        if (cmd == "test-validate-decision-consistency")
            return test_validate_decision_consistency();
        if (cmd == "test-default-deny") return test_default_deny();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    int failures = 0;
    failures += test_build_all_gates_pass();
    failures += test_build_deny_no_approval();
    failures += test_build_deny_config_invalid();
    failures += test_build_deny_registry_mismatch();
    failures += test_build_deny_security_constraints();
    failures += test_validate_format_version();
    failures += test_validate_decision_consistency();
    failures += test_default_deny();

    if (failures == 0) {
        std::cout << "\nAll opt-in guard tests passed.\n";
    }
    return failures;
}

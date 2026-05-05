#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"

#include <string>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(OptInDecision decision) {
    switch (decision) {
    case OptInDecision::Allow:
        return "Allow";
    case OptInDecision::Deny:
        return "Deny";
    case OptInDecision::DenyWithOverride:
        return "DenyWithOverride";
    }
    return "Deny";
}

[[nodiscard]] std::string_view to_string_view(OptInDenialReason reason) {
    switch (reason) {
    case OptInDenialReason::NoApproval:
        return "NoApproval";
    case OptInDenialReason::ConfigInvalid:
        return "ConfigInvalid";
    case OptInDenialReason::RegistryMismatch:
        return "RegistryMismatch";
    case OptInDenialReason::ReadinessNotMet:
        return "ReadinessNotMet";
    case OptInDenialReason::ExplicitDeny:
        return "ExplicitDeny";
    case OptInDenialReason::ScopeExceeded:
        return "ScopeExceeded";
    }
    return "ExplicitDeny";
}

[[nodiscard]] ProviderOptInDecisionReportValidationResult
validate_provider_opt_in_decision_report(const ProviderOptInDecisionReport &report) {
    DiagnosticBag diagnostics;

    // 校验 format_version
    if (report.format_version != kProviderOptInDecisionReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 decision 与 is_real_provider_traffic_allowed 一致性
    if (report.decision != OptInDecision::Allow && report.is_real_provider_traffic_allowed) {
        diagnostics.error()
            .message("is_real_provider_traffic_allowed=true is inconsistent with decision=" +
                     std::string(to_string_view(report.decision)))
            .emit();
    }
    if (report.decision == OptInDecision::Allow && !report.is_real_provider_traffic_allowed) {
        diagnostics.error()
            .message("is_real_provider_traffic_allowed=false is inconsistent with decision=Allow")
            .emit();
    }

    // 校验 gates 计数一致性
    int actual_passed = 0;
    int actual_failed = 0;
    for (const auto &gate : report.gate_checks) {
        if (gate.passed) {
            ++actual_passed;
        } else {
            ++actual_failed;
        }
    }
    if (actual_passed != report.gates_passed) {
        diagnostics.error().message("gates_passed count mismatch").emit();
    }
    if (actual_failed != report.gates_failed) {
        diagnostics.error().message("gates_failed count mismatch").emit();
    }

    // 校验 denial_reasons 非空 when decision != Allow
    if (report.decision != OptInDecision::Allow && report.denial_reasons.empty()) {
        diagnostics.error()
            .message("denial_reasons must not be empty when decision is not Allow")
            .emit();
    }

    return ProviderOptInDecisionReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderOptInDecisionReportResult build_provider_opt_in_decision_report(
    const ApprovalReceipt &approval_receipt,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderSelectionPlan &selection_plan) {

    DiagnosticBag diagnostics;
    ProviderOptInDecisionReport report;

    // 填充基础信息
    report.workflow_canonical_name = approval_receipt.workflow_canonical_name;
    report.session_id = approval_receipt.session_id;
    report.run_id = approval_receipt.run_id;

    // 填充上游 identity 引用
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;

    // Gate 1: Approval gate - 检查审批是否通过
    OptInGateCheck approval_gate;
    approval_gate.gate_name = "approval-receipt";
    approval_gate.evidence_reference = approval_receipt.approval_decision_identity;
    if (approval_receipt.is_approved &&
        approval_receipt.final_decision == ApprovalDecision::Approved) {
        approval_gate.passed = true;
    } else {
        approval_gate.passed = false;
        approval_gate.denial_reason = OptInDenialReason::NoApproval;
    }
    report.gate_checks.push_back(std::move(approval_gate));

    // Gate 2: Config validation gate - 检查配置是否有效
    OptInGateCheck config_gate;
    config_gate.gate_name = "config-bundle-validation";
    config_gate.evidence_reference = config_report.config_bundle_identity;
    if (config_report.invalid_count == 0 && config_report.missing_count == 0) {
        config_gate.passed = true;
    } else {
        config_gate.passed = false;
        config_gate.denial_reason = OptInDenialReason::ConfigInvalid;
    }
    report.gate_checks.push_back(std::move(config_gate));

    // Gate 3: Registry selection gate - 检查 provider 注册和选择是否匹配
    OptInGateCheck registry_gate;
    registry_gate.gate_name = "registry-selection-plan";
    registry_gate.evidence_reference =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    if (selection_plan.selection_status == ProviderSelectionStatus::Selected ||
        selection_plan.selection_status == ProviderSelectionStatus::FallbackSelected) {
        registry_gate.passed = true;
    } else {
        registry_gate.passed = false;
        registry_gate.denial_reason = OptInDenialReason::RegistryMismatch;
    }
    report.gate_checks.push_back(std::move(registry_gate));

    // Gate 4: Security constraints gate - 确保 config 不涉及敏感操作
    OptInGateCheck security_gate;
    security_gate.gate_name = "security-constraints";
    security_gate.evidence_reference = config_report.config_bundle_identity;
    if (!config_report.reads_secret_value && !config_report.opens_network_connection) {
        security_gate.passed = true;
    } else {
        security_gate.passed = false;
        security_gate.denial_reason = OptInDenialReason::ReadinessNotMet;
    }
    report.gate_checks.push_back(std::move(security_gate));

    // 汇总 gates 结果
    for (const auto &gate : report.gate_checks) {
        if (gate.passed) {
            ++report.gates_passed;
        } else {
            ++report.gates_failed;
            if (gate.denial_reason.has_value()) {
                report.denial_reasons.push_back(*gate.denial_reason);
            }
        }
    }

    // 决定逻辑：所有 gate 通过时 Allow，否则 Deny
    if (report.gates_failed == 0) {
        report.decision = OptInDecision::Allow;
        report.is_real_provider_traffic_allowed = true;
        report.decision_summary = "all gates passed; real provider traffic allowed";
        report.denial_reason_summary = "none";
    } else {
        report.decision = OptInDecision::Deny;
        report.is_real_provider_traffic_allowed = false;
        report.decision_summary = "one or more gates failed; real provider traffic denied";

        // 构建 denial reason summary
        std::string reasons;
        for (const auto &reason : report.denial_reasons) {
            if (!reasons.empty()) {
                reasons += ", ";
            }
            reasons += to_string_view(reason);
        }
        report.denial_reason_summary = reasons;
    }

    return ProviderOptInDecisionReportResult{std::move(report), std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

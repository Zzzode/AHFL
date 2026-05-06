#include "ahfl/durable_store_import/provider_runtime_policy.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(PolicyDecision decision) {
    switch (decision) {
    case PolicyDecision::Permit:
        return "Permit";
    case PolicyDecision::Deny:
        return "Deny";
    case PolicyDecision::DenyWithWarning:
        return "DenyWithWarning";
    }
    return "Deny";
}

[[nodiscard]] std::string_view to_string_view(PolicyViolationType violation) {
    switch (violation) {
    case PolicyViolationType::OptInNotGranted:
        return "OptInNotGranted";
    case PolicyViolationType::ApprovalMissing:
        return "ApprovalMissing";
    case PolicyViolationType::ConfigInvalid:
        return "ConfigInvalid";
    case PolicyViolationType::RegistryMismatch:
        return "RegistryMismatch";
    case PolicyViolationType::ReadinessNotMet:
        return "ReadinessNotMet";
    case PolicyViolationType::ScopeExceeded:
        return "ScopeExceeded";
    case PolicyViolationType::EvidenceStale:
        return "EvidenceStale";
    }
    return "OptInNotGranted";
}

[[nodiscard]] ProviderRuntimePolicyReportValidationResult
validate_provider_runtime_policy_report(const ProviderRuntimePolicyReport &report) {
    DiagnosticBag diagnostics;

    // 校验 format_version
    if (report.format_version != kProviderRuntimePolicyReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 decision 与 is_execution_permitted 一致性
    if (report.decision != PolicyDecision::Permit && report.is_execution_permitted) {
        diagnostics.error()
            .message("is_execution_permitted=true is inconsistent with decision=" +
                     std::string(to_string_view(report.decision)))
            .emit();
    }
    if (report.decision == PolicyDecision::Permit && !report.is_execution_permitted) {
        diagnostics.error()
            .message("is_execution_permitted=false is inconsistent with decision=Permit")
            .emit();
    }

    // 校验 gates 计数一致性
    int actual_passed = 0;
    int actual_failed = 0;
    for (const auto &gate : report.policy_gates) {
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

    // 校验 violation 计数一致性
    int actual_blocking = 0;
    for (const auto &gate : report.policy_gates) {
        if (!gate.passed) {
            actual_blocking += static_cast<int>(gate.violations.size());
        }
    }
    if (actual_blocking != report.blocking_violation_count) {
        diagnostics.error().message("blocking_violation_count mismatch").emit();
    }

    return ProviderRuntimePolicyReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderRuntimePolicyReportResult build_provider_runtime_policy_report(
    const ProviderOptInDecisionReport &opt_in_report,
    const ApprovalReceipt &approval_receipt,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderSelectionPlan &selection_plan,
    const ProviderProductionReadinessEvidence &readiness_evidence) {

    DiagnosticBag diagnostics;
    ProviderRuntimePolicyReport report;

    // 填充基础信息（从 opt-in report 继承 session 信息）
    report.workflow_canonical_name = opt_in_report.workflow_canonical_name;
    report.session_id = opt_in_report.session_id;
    report.run_id = opt_in_report.run_id;

    // 填充上游 identity 引用
    report.opt_in_decision_report_identity = "opt-in-decision-report::" + opt_in_report.session_id;
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.registry_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    report.production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // Gate 1: Opt-In gate - 检查 opt-in decision 是否为 Allow
    PolicyGateResult opt_in_gate;
    opt_in_gate.gate_name = "opt_in";
    opt_in_gate.evidence_reference = report.opt_in_decision_report_identity;
    if (opt_in_report.decision == OptInDecision::Allow &&
        opt_in_report.is_real_provider_traffic_allowed) {
        opt_in_gate.passed = true;
    } else {
        opt_in_gate.passed = false;
        opt_in_gate.violations.push_back(PolicyViolationType::OptInNotGranted);
    }
    report.policy_gates.push_back(std::move(opt_in_gate));

    // Gate 2: Approval gate - 检查审批是否通过
    PolicyGateResult approval_gate;
    approval_gate.gate_name = "approval";
    approval_gate.evidence_reference = approval_receipt.approval_decision_identity;
    if (approval_receipt.is_approved &&
        approval_receipt.final_decision == ApprovalDecision::Approved) {
        approval_gate.passed = true;
    } else {
        approval_gate.passed = false;
        approval_gate.violations.push_back(PolicyViolationType::ApprovalMissing);
    }
    report.policy_gates.push_back(std::move(approval_gate));

    // Gate 3: Config validation gate - 检查配置是否有效
    PolicyGateResult config_gate;
    config_gate.gate_name = "config";
    config_gate.evidence_reference = config_report.config_bundle_identity;
    if (config_report.invalid_count == 0 && config_report.missing_count == 0) {
        config_gate.passed = true;
    } else {
        config_gate.passed = false;
        config_gate.violations.push_back(PolicyViolationType::ConfigInvalid);
    }
    report.policy_gates.push_back(std::move(config_gate));

    // Gate 4: Registry selection gate - 检查 provider 注册和选择是否匹配
    PolicyGateResult registry_gate;
    registry_gate.gate_name = "registry";
    registry_gate.evidence_reference =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    if (selection_plan.selection_status == ProviderSelectionStatus::Selected ||
        selection_plan.selection_status == ProviderSelectionStatus::FallbackSelected) {
        registry_gate.passed = true;
    } else {
        registry_gate.passed = false;
        registry_gate.violations.push_back(PolicyViolationType::RegistryMismatch);
    }
    report.policy_gates.push_back(std::move(registry_gate));

    // Gate 5: Production readiness gate - 检查就绪证据
    PolicyGateResult readiness_gate;
    readiness_gate.gate_name = "readiness";
    readiness_gate.evidence_reference =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;
    if (readiness_evidence.blocking_issue_count == 0 &&
        readiness_evidence.security_evidence_passed &&
        readiness_evidence.recovery_evidence_passed &&
        readiness_evidence.compatibility_evidence_passed) {
        readiness_gate.passed = true;
    } else {
        readiness_gate.passed = false;
        readiness_gate.violations.push_back(PolicyViolationType::ReadinessNotMet);
    }
    report.policy_gates.push_back(std::move(readiness_gate));

    // 汇总 gates 结果
    for (const auto &gate : report.policy_gates) {
        if (gate.passed) {
            ++report.gates_passed;
        } else {
            ++report.gates_failed;
            report.blocking_violation_count += static_cast<int>(gate.violations.size());
        }
    }

    // 决定逻辑：所有 gate 通过时 Permit，否则 Deny
    if (report.gates_failed == 0) {
        report.decision = PolicyDecision::Permit;
        report.is_execution_permitted = true;
        report.policy_summary = "all policy gates passed; execution permitted";
        report.violation_summary = "none";
        report.next_operator_action = "proceed with provider execution";
    } else {
        report.decision = PolicyDecision::Deny;
        report.is_execution_permitted = false;
        report.policy_summary = "one or more policy gates failed; execution denied";

        // 构建 violation summary
        std::string violations;
        for (const auto &gate : report.policy_gates) {
            if (!gate.passed) {
                for (const auto &v : gate.violations) {
                    if (!violations.empty()) {
                        violations += ", ";
                    }
                    violations += to_string_view(v);
                }
            }
        }
        report.violation_summary = violations;
        report.next_operator_action = "resolve policy violations before retrying";
    }

    return ProviderRuntimePolicyReportResult{std::move(report), std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

#pragma once

#include "ahfl/durable_store_import/provider_approval_workflow.hpp"
#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"
#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/support/diagnostics.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Runtime Policy Report 格式版本常量
inline constexpr std::string_view kProviderRuntimePolicyReportFormatVersion =
    "ahfl.durable-store-import-provider-runtime-policy-report.v1";

// Policy 决策枚举
enum class PolicyDecision {
    Permit,         // 允许执行
    Deny,           // 拒绝（默认）
    DenyWithWarning // 拒绝但附带 warning
};

// Policy 违规类型枚举
enum class PolicyViolationType {
    OptInNotGranted,  // Opt-in 未授予
    ApprovalMissing,  // 缺少审批
    ConfigInvalid,    // 配置无效
    RegistryMismatch, // Registry 不匹配
    ReadinessNotMet,  // 就绪检查未通过
    ScopeExceeded,    // 范围超限
    EvidenceStale,    // 证据过期
};

// 单个 policy gate 检查结果
struct PolicyGateResult {
    std::string gate_name; // "opt_in", "approval", "config", "registry", "readiness"
    bool passed{false};
    std::vector<PolicyViolationType> violations;
    std::string evidence_reference;
};

// Provider Runtime Policy Report - v0.49
// 聚合所有 policy evidence，输出统一的 permit / deny 决策
struct ProviderRuntimePolicyReport {
    std::string format_version{std::string(kProviderRuntimePolicyReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 上游 artifact identity 引用
    std::string opt_in_decision_report_identity;
    std::string approval_receipt_identity;
    std::string config_validation_report_identity;
    std::string registry_selection_plan_identity;
    std::string production_readiness_evidence_identity;

    // Policy gates 检查结果
    std::vector<PolicyGateResult> policy_gates;

    // 汇总决策
    PolicyDecision decision{PolicyDecision::Deny};
    int gates_passed{0};
    int gates_failed{0};
    int blocking_violation_count{0};
    int warning_violation_count{0};

    // 汇总信息
    std::string policy_summary;
    std::string violation_summary;
    std::string next_operator_action;

    // 安全默认：false
    bool is_execution_permitted{false};
};

// 验证结果
struct ProviderRuntimePolicyReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果
struct ProviderRuntimePolicyReportResult {
    std::optional<ProviderRuntimePolicyReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证函数
[[nodiscard]] ProviderRuntimePolicyReportValidationResult
validate_provider_runtime_policy_report(const ProviderRuntimePolicyReport &report);

// 构建函数 - 聚合所有 policy evidence
[[nodiscard]] ProviderRuntimePolicyReportResult
build_provider_runtime_policy_report(const ProviderOptInDecisionReport &opt_in_report,
                                     const ApprovalReceipt &approval_receipt,
                                     const ProviderConfigBundleValidationReport &config_report,
                                     const ProviderSelectionPlan &selection_plan,
                                     const ProviderProductionReadinessEvidence &readiness_evidence);

// 辅助函数：将 PolicyDecision 转换为字符串
[[nodiscard]] std::string_view to_string_view(PolicyDecision decision);

// 辅助函数：将 PolicyViolationType 转换为字符串
[[nodiscard]] std::string_view to_string_view(PolicyViolationType violation);

} // namespace ahfl::durable_store_import

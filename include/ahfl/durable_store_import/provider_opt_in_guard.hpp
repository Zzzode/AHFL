#pragma once

#include "ahfl/durable_store_import/provider_approval_workflow.hpp"
#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/support/diagnostics.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Opt-In Decision Report 格式版本常量
inline constexpr std::string_view kProviderOptInDecisionReportFormatVersion =
    "ahfl.durable-store-import-provider-opt-in-decision-report.v1";

// Opt-In 决定枚举
enum class OptInDecision {
    Allow,           // 允许真实 provider traffic
    Deny,            // 拒绝（默认）
    DenyWithOverride // 拒绝但有 scoped override
};

// 拒绝原因枚举
enum class OptInDenialReason {
    NoApproval,       // 缺少有效的审批
    ConfigInvalid,    // 配置校验失败
    RegistryMismatch, // Provider 注册信息不匹配
    ReadinessNotMet,  // 就绪检查未通过
    ExplicitDeny,     // 显式拒绝
    ScopeExceeded,    // 范围超限
};

// Gate 检查项
struct OptInGateCheck {
    std::string gate_name;
    bool passed{false};
    std::optional<OptInDenialReason> denial_reason;
    std::string evidence_reference;
};

// Scoped Override 结构
struct ScopedOverride {
    std::string override_scope;
    std::string override_justification;
    std::string override_approver;
    bool is_valid{false};
};

// Opt-In Decision Report - v0.48
// 输出给 operator-facing 的 opt-in 决定报告
struct ProviderOptInDecisionReport {
    std::string format_version{std::string(kProviderOptInDecisionReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 上游 artifact 引用
    std::string approval_receipt_identity;
    std::string config_validation_report_identity;
    std::string registry_selection_plan_identity;

    // Gate 检查结果
    std::vector<OptInGateCheck> gate_checks;
    std::optional<ScopedOverride> scoped_override;

    // 最终决定
    OptInDecision decision{OptInDecision::Deny};
    std::vector<OptInDenialReason> denial_reasons;

    // 汇总
    int gates_passed{0};
    int gates_failed{0};
    std::string decision_summary;
    std::string denial_reason_summary;

    // 安全默认：false
    bool is_real_provider_traffic_allowed{false};
};

// 验证结果
struct ProviderOptInDecisionReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果
struct ProviderOptInDecisionReportResult {
    std::optional<ProviderOptInDecisionReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证函数
[[nodiscard]] ProviderOptInDecisionReportValidationResult
validate_provider_opt_in_decision_report(const ProviderOptInDecisionReport &report);

// 构建函数
// 从 ApprovalReceipt、ProviderConfigBundleValidationReport 和 ProviderSelectionPlan 构建 OptInDecisionReport
[[nodiscard]] ProviderOptInDecisionReportResult
build_provider_opt_in_decision_report(const ApprovalReceipt &approval_receipt,
                                      const ProviderConfigBundleValidationReport &config_report,
                                      const ProviderSelectionPlan &selection_plan);

// 辅助函数：将 OptInDecision 转换为字符串
[[nodiscard]] std::string_view to_string_view(OptInDecision decision);

// 辅助函数：将 OptInDenialReason 转换为字符串
[[nodiscard]] std::string_view to_string_view(OptInDenialReason reason);

} // namespace ahfl::durable_store_import

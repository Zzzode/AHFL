#pragma once

#include "ahfl/durable_store_import/provider_approval_workflow.hpp"
#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/durable_store_import/provider_conformance.hpp"
#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"
#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"
#include "ahfl/durable_store_import/provider_runtime_policy.hpp"
#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"
#include "ahfl/support/diagnostics.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Production Integration Dry Run Report 格式版本常量
inline constexpr std::string_view kProviderProductionIntegrationDryRunReportFormatVersion =
    "ahfl.durable-store-import-provider-production-integration-dry-run-report.v1";

// Production Readiness State 枚举
enum class ProductionReadinessState {
    ReadyForControlledRollout, // 完全就绪，可进入 controlled rollout
    ReadyWithConditions,       // 有条件就绪，需要处理指定条件
    Blocked,                   // 被阻塞，存在 blocking items
    InsufficientEvidence,      // 证据不足，缺少必要 evidence
};

// Evidence Chain Item - 单个 evidence 的状态描述
struct EvidenceChainItem {
    std::string evidence_type;     // "conformance", "schema_compatibility", "config_validation",
                                   // "release_archive", "approval", "opt_in", "runtime_policy"
    std::string evidence_identity; // 引用的 artifact identity
    std::string format_version;    // 该 evidence 的 format_version
    bool is_present{false};        // evidence 是否存在
    bool is_valid{false};          // evidence 是否通过校验
    bool is_fresh{true};           // evidence 是否新鲜（未过期）
};

// Blocking Item - 阻塞项描述
struct BlockingItem {
    std::string block_type;   // "missing_evidence", "invalid_evidence", "policy_violation", etc.
    std::string block_reason; // 阻塞原因描述
    std::string responsible_artifact; // 负责的 artifact 标识
    std::string suggested_action;     // 建议的解除阻塞操作
};

// Next Operator Action - 下一步操作建议
struct NextOperatorAction {
    std::string action_type;        // "approve", "fix_config", "retry_evidence", "resolve_blocker"
    std::string action_description; // 操作描述
    std::string action_target;      // 操作目标（artifact identity 或具体对象）
    int priority{1};                // 优先级（1 = 最高）
};

// Provider Production Integration Dry Run Report - v0.50
// 终点节点：聚合 v0.43-v0.49 全部 evidence chain
struct ProviderProductionIntegrationDryRunReport {
    std::string format_version{
        std::string(kProviderProductionIntegrationDryRunReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // ===== 完整 evidence chain 引用 =====
    // v0.43: Provider Conformance Report
    std::string conformance_report_identity;
    // v0.44: Provider Schema Compatibility Report
    std::string schema_compatibility_report_identity;
    // v0.45: Provider Config Bundle Validation Report
    std::string config_validation_report_identity;
    // v0.46: Release Evidence Archive Manifest
    std::string release_evidence_archive_manifest_identity;
    // v0.47: Approval Receipt
    std::string approval_receipt_identity;
    // v0.48: Provider Opt-In Decision Report
    std::string opt_in_decision_report_identity;
    // v0.49: Provider Runtime Policy Report
    std::string runtime_policy_report_identity;

    // ===== Evidence chain summary =====
    std::vector<EvidenceChainItem> evidence_chain;
    int total_evidence_count{7};
    int valid_evidence_count{0};
    int invalid_evidence_count{0};
    int missing_evidence_count{0};

    // ===== Readiness state =====
    ProductionReadinessState readiness_state{ProductionReadinessState::Blocked};

    // ===== Blocking summary =====
    std::vector<BlockingItem> blocking_items;
    int blocking_item_count{0};
    std::string blocking_summary;

    // ===== Next actions =====
    std::vector<NextOperatorAction> next_operator_actions;

    // ===== Final summary =====
    std::string dry_run_summary;
    // 安全默认：false - 不自动批准 production integration
    bool is_ready_for_controlled_rollout{false};
    // 默认仍为 non-mutating 模式
    bool is_non_mutating_mode{true};
    // 生成时间戳（ISO 8601）
    std::string generated_at;
};

// 验证结果
struct ProviderProductionIntegrationDryRunReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果
struct ProviderProductionIntegrationDryRunReportResult {
    std::optional<ProviderProductionIntegrationDryRunReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证函数
[[nodiscard]] ProviderProductionIntegrationDryRunReportValidationResult
validate_provider_production_integration_dry_run_report(
    const ProviderProductionIntegrationDryRunReport &report);

// 构建函数 - 终点汇总：聚合 v0.43-v0.49 全部 evidence
[[nodiscard]] ProviderProductionIntegrationDryRunReportResult
build_provider_production_integration_dry_run_report(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ReleaseEvidenceArchiveManifest &evidence_archive,
    const ApprovalReceipt &approval_receipt,
    const ProviderOptInDecisionReport &opt_in_report,
    const ProviderRuntimePolicyReport &runtime_policy_report);

// 辅助函数：将 ProductionReadinessState 转换为字符串
[[nodiscard]] std::string_view to_string_view(ProductionReadinessState state);

} // namespace ahfl::durable_store_import

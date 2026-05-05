#include "ahfl/durable_store_import/provider_production_integration_dry_run.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

[[nodiscard]] std::string_view to_string_view(ProductionReadinessState state) {
    switch (state) {
    case ProductionReadinessState::ReadyForControlledRollout:
        return "ReadyForControlledRollout";
    case ProductionReadinessState::ReadyWithConditions:
        return "ReadyWithConditions";
    case ProductionReadinessState::Blocked:
        return "Blocked";
    case ProductionReadinessState::InsufficientEvidence:
        return "InsufficientEvidence";
    }
    return "Blocked";
}

[[nodiscard]] ProviderProductionIntegrationDryRunReportValidationResult
validate_provider_production_integration_dry_run_report(
    const ProviderProductionIntegrationDryRunReport &report) {
    DiagnosticBag diagnostics;

    // 校验 format_version
    if (report.format_version != kProviderProductionIntegrationDryRunReportFormatVersion) {
        diagnostics.error().message("unsupported format_version: " + report.format_version).emit();
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty()) {
        diagnostics.error().message("workflow_canonical_name is required").emit();
    }
    if (report.session_id.empty()) {
        diagnostics.error().message("session_id is required").emit();
    }

    // 校验 readiness_state 与 is_ready_for_controlled_rollout 一致性
    if (report.readiness_state != ProductionReadinessState::ReadyForControlledRollout &&
        report.is_ready_for_controlled_rollout) {
        diagnostics.error()
            .message("is_ready_for_controlled_rollout=true is inconsistent with readiness_state=" +
                     std::string(to_string_view(report.readiness_state)))
            .emit();
    }
    if (report.readiness_state == ProductionReadinessState::ReadyForControlledRollout &&
        !report.is_ready_for_controlled_rollout) {
        diagnostics.error()
            .message("is_ready_for_controlled_rollout=false is inconsistent with "
                     "readiness_state=ReadyForControlledRollout")
            .emit();
    }

    // 校验 is_non_mutating_mode 必须为 true（当前版本约束）
    if (!report.is_non_mutating_mode) {
        diagnostics.error()
            .message("is_non_mutating_mode must be true in current version")
            .emit();
    }

    // 校验 evidence chain 计数一致性
    int actual_valid = 0;
    int actual_invalid = 0;
    int actual_missing = 0;
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            ++actual_missing;
        } else if (!item.is_valid) {
            ++actual_invalid;
        } else {
            ++actual_valid;
        }
    }
    if (actual_valid != report.valid_evidence_count) {
        diagnostics.error().message("valid_evidence_count mismatch").emit();
    }
    if (actual_invalid != report.invalid_evidence_count) {
        diagnostics.error().message("invalid_evidence_count mismatch").emit();
    }
    if (actual_missing != report.missing_evidence_count) {
        diagnostics.error().message("missing_evidence_count mismatch").emit();
    }

    // 校验 blocking_item_count 一致性
    if (static_cast<int>(report.blocking_items.size()) != report.blocking_item_count) {
        diagnostics.error().message("blocking_item_count mismatch").emit();
    }

    return ProviderProductionIntegrationDryRunReportValidationResult{std::move(diagnostics)};
}

[[nodiscard]] ProviderProductionIntegrationDryRunReportResult
build_provider_production_integration_dry_run_report(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ReleaseEvidenceArchiveManifest &evidence_archive,
    const ApprovalReceipt &approval_receipt,
    const ProviderOptInDecisionReport &opt_in_report,
    const ProviderRuntimePolicyReport &runtime_policy_report) {

    DiagnosticBag diagnostics;
    ProviderProductionIntegrationDryRunReport report;

    // 填充基础信息（从 conformance report 继承 session 信息）
    report.workflow_canonical_name = conformance_report.workflow_canonical_name;
    report.session_id = conformance_report.session_id;
    report.run_id = conformance_report.run_id;

    // 填充 evidence chain identity 引用
    report.conformance_report_identity =
        conformance_report.durable_store_import_provider_conformance_report_identity;
    report.schema_compatibility_report_identity =
        "schema-compatibility-report::" + schema_report.session_id;
    report.config_validation_report_identity = config_report.config_bundle_identity;
    report.release_evidence_archive_manifest_identity =
        evidence_archive.durable_store_import_provider_conformance_report_identity;
    report.approval_receipt_identity = approval_receipt.approval_request_identity;
    report.opt_in_decision_report_identity =
        "opt-in-decision-report::" + opt_in_report.session_id;
    report.runtime_policy_report_identity =
        "runtime-policy-report::" + runtime_policy_report.session_id;

    // ===== 构建 Evidence Chain =====

    // v0.43: Conformance Report
    {
        EvidenceChainItem item;
        item.evidence_type = "conformance";
        item.evidence_identity = report.conformance_report_identity;
        item.format_version = conformance_report.format_version;
        item.is_present = true;
        item.is_valid = (conformance_report.fail_count == 0);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.44: Schema Compatibility Report
    {
        EvidenceChainItem item;
        item.evidence_type = "schema_compatibility";
        item.evidence_identity = report.schema_compatibility_report_identity;
        item.format_version = schema_report.format_version;
        item.is_present = true;
        item.is_valid = (schema_report.incompatible_count == 0 && !schema_report.has_schema_drift);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.45: Config Bundle Validation Report
    {
        EvidenceChainItem item;
        item.evidence_type = "config_validation";
        item.evidence_identity = report.config_validation_report_identity;
        item.format_version = config_report.format_version;
        item.is_present = true;
        item.is_valid = (config_report.invalid_count == 0 && config_report.missing_count == 0);
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.46: Release Evidence Archive Manifest
    {
        EvidenceChainItem item;
        item.evidence_type = "release_archive";
        item.evidence_identity = report.release_evidence_archive_manifest_identity;
        item.format_version = evidence_archive.format_version;
        item.is_present = true;
        item.is_valid = evidence_archive.is_release_ready;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.47: Approval Receipt
    {
        EvidenceChainItem item;
        item.evidence_type = "approval";
        item.evidence_identity = report.approval_receipt_identity;
        item.format_version = approval_receipt.format_version;
        item.is_present = true;
        item.is_valid = approval_receipt.is_approved;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.48: Opt-In Decision Report
    {
        EvidenceChainItem item;
        item.evidence_type = "opt_in";
        item.evidence_identity = report.opt_in_decision_report_identity;
        item.format_version = opt_in_report.format_version;
        item.is_present = true;
        item.is_valid = opt_in_report.is_real_provider_traffic_allowed;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // v0.49: Runtime Policy Report
    {
        EvidenceChainItem item;
        item.evidence_type = "runtime_policy";
        item.evidence_identity = report.runtime_policy_report_identity;
        item.format_version = runtime_policy_report.format_version;
        item.is_present = true;
        item.is_valid = runtime_policy_report.is_execution_permitted;
        item.is_fresh = true;
        report.evidence_chain.push_back(std::move(item));
    }

    // ===== 汇总 evidence chain 计数 =====
    report.total_evidence_count = static_cast<int>(report.evidence_chain.size());
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            ++report.missing_evidence_count;
        } else if (!item.is_valid) {
            ++report.invalid_evidence_count;
        } else {
            ++report.valid_evidence_count;
        }
    }

    // ===== 构建 blocking items =====
    for (const auto &item : report.evidence_chain) {
        if (!item.is_present) {
            BlockingItem blocker;
            blocker.block_type = "missing_evidence";
            blocker.block_reason = item.evidence_type + " evidence is missing";
            blocker.responsible_artifact = item.evidence_identity;
            blocker.suggested_action = "generate " + item.evidence_type + " report";
            report.blocking_items.push_back(std::move(blocker));
        } else if (!item.is_valid) {
            BlockingItem blocker;
            blocker.block_type = "invalid_evidence";
            blocker.block_reason = item.evidence_type + " evidence is invalid";
            blocker.responsible_artifact = item.evidence_identity;
            blocker.suggested_action = "resolve " + item.evidence_type + " failures";
            report.blocking_items.push_back(std::move(blocker));
        }
    }
    report.blocking_item_count = static_cast<int>(report.blocking_items.size());

    // ===== 确定 readiness state =====
    if (report.missing_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::InsufficientEvidence;
    } else if (report.invalid_evidence_count > 0) {
        report.readiness_state = ProductionReadinessState::Blocked;
    } else {
        report.readiness_state = ProductionReadinessState::ReadyForControlledRollout;
    }

    // ===== 设定 is_ready_for_controlled_rollout =====
    report.is_ready_for_controlled_rollout =
        (report.readiness_state == ProductionReadinessState::ReadyForControlledRollout);

    // ===== 构建 next operator actions =====
    if (report.is_ready_for_controlled_rollout) {
        NextOperatorAction action;
        action.action_type = "proceed";
        action.action_description = "all evidence passed; proceed with controlled rollout";
        action.action_target = report.workflow_canonical_name;
        action.priority = 1;
        report.next_operator_actions.push_back(std::move(action));
    } else {
        int priority = 1;
        for (const auto &blocker : report.blocking_items) {
            NextOperatorAction action;
            action.action_type = "resolve_blocker";
            action.action_description = blocker.suggested_action;
            action.action_target = blocker.responsible_artifact;
            action.priority = priority++;
            report.next_operator_actions.push_back(std::move(action));
        }
    }

    // ===== 构建 summary =====
    if (report.is_ready_for_controlled_rollout) {
        report.blocking_summary = "none";
        report.dry_run_summary =
            "all evidence chain items passed; production integration dry run succeeded; "
            "ready for controlled rollout";
    } else {
        std::string blocking_desc;
        for (const auto &blocker : report.blocking_items) {
            if (!blocking_desc.empty()) {
                blocking_desc += "; ";
            }
            blocking_desc += blocker.block_reason;
        }
        report.blocking_summary = blocking_desc;
        report.dry_run_summary =
            "production integration dry run blocked: " + blocking_desc;
    }

    // non-mutating 模式始终为 true
    report.is_non_mutating_mode = true;

    return ProviderProductionIntegrationDryRunReportResult{std::move(report),
                                                           std::move(diagnostics)};
}

} // namespace ahfl::durable_store_import

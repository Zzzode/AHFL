#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::durable_store_import {

// Approval Request 格式版本常量
inline constexpr std::string_view kProviderApprovalRequestFormatVersion =
    "ahfl.durable-store-import-provider-approval-request.v1";

// Approval Decision 格式版本常量
inline constexpr std::string_view kProviderApprovalDecisionFormatVersion =
    "ahfl.durable-store-import-provider-approval-decision.v1";

// Approval Receipt 格式版本常量
inline constexpr std::string_view kProviderApprovalReceiptFormatVersion =
    "ahfl.durable-store-import-provider-approval-receipt.v1";

// 审批决定枚举
enum class ApprovalDecision {
    Approved,   // 已批准
    Rejected,   // 已拒绝
    Deferred,   // 延迟决定
};

// 审批请求 - v0.47
// 用于表达生产集成的人工审批输入
struct ApprovalRequest {
    std::string format_version{std::string(kProviderApprovalRequestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 上游 evidence 引用
    std::string release_evidence_archive_manifest_identity;
    std::string production_readiness_evidence_identity;

    // 请求者身份与审批范围
    std::string requestor_identity;
    std::string approval_scope;
    std::string request_justification;
};

// 审批决定记录 - v0.47
// 用于表达审批决定、拒绝原因与证据链
struct ApprovalDecisionRecord {
    std::string format_version{std::string(kProviderApprovalDecisionFormatVersion)};
    std::string approval_request_identity;
    ApprovalDecision decision{ApprovalDecision::Rejected};
    std::string decision_reason;
    std::optional<std::string> approver_identity;
    std::optional<std::string> rejection_details;
    std::optional<std::string> conditions;
};

// 审批回执 - v0.47
// 输出给 operator-facing 的审批结果
struct ApprovalReceipt {
    std::string format_version{std::string(kProviderApprovalReceiptFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 上游 artifact 引用
    std::string approval_request_identity;
    std::string approval_decision_identity;

    // 最终决定
    ApprovalDecision final_decision{ApprovalDecision::Rejected};

    // Evidence binding
    std::string release_evidence_archive_manifest_identity;
    std::string production_readiness_evidence_identity;

    // 汇总摘要
    std::string receipt_summary;
    std::string blocking_reason_summary;

    // 状态标志（安全默认：false）
    bool is_approved{false};
};

// 验证结果
struct ApprovalRequestValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ApprovalDecisionRecordValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ApprovalReceiptValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果
struct ApprovalRequestResult {
    std::optional<ApprovalRequest> request;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ApprovalDecisionRecordResult {
    std::optional<ApprovalDecisionRecord> decision;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ApprovalReceiptResult {
    std::optional<ApprovalReceipt> receipt;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证函数
[[nodiscard]] ApprovalRequestValidationResult validate_approval_request(
    const ApprovalRequest &request);

[[nodiscard]] ApprovalDecisionRecordValidationResult validate_approval_decision_record(
    const ApprovalDecisionRecord &decision);

[[nodiscard]] ApprovalReceiptValidationResult validate_approval_receipt(
    const ApprovalReceipt &receipt);

// 构建函数
// 从 ReleaseEvidenceArchiveManifest 和 ProviderProductionReadinessEvidence 构建 ApprovalRequest
[[nodiscard]] ApprovalRequestResult build_approval_request(
    const ReleaseEvidenceArchiveManifest &archive,
    const ProviderProductionReadinessEvidence &readiness);

// 从 ApprovalRequest 和 ApprovalDecisionRecord 构建 ApprovalReceipt
[[nodiscard]] ApprovalReceiptResult build_approval_receipt(
    const ApprovalRequest &request,
    const ApprovalDecisionRecord &decision);

// 辅助函数：将 ApprovalDecision 转换为字符串
[[nodiscard]] std::string_view to_string_view(ApprovalDecision decision);

} // namespace ahfl::durable_store_import

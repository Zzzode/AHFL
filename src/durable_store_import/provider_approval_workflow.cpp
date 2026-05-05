#include "ahfl/durable_store_import/provider_approval_workflow.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_APPROVAL_WORKFLOW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

} // namespace

std::string_view to_string_view(ApprovalDecision decision) {
    switch (decision) {
    case ApprovalDecision::Approved:
        return "Approved";
    case ApprovalDecision::Rejected:
        return "Rejected";
    case ApprovalDecision::Deferred:
        return "Deferred";
    }
    return "Rejected";
}

ApprovalRequestValidationResult validate_approval_request(const ApprovalRequest &request) {
    ApprovalRequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (request.format_version != kProviderApprovalRequestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "approval request format_version must be '" +
                std::string(kProviderApprovalRequestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty() || request.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "approval request workflow_canonical_name and session_id must not be empty");
    }

    if (request.release_evidence_archive_manifest_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "approval request release_evidence_archive_manifest_identity must not be empty");
    }

    if (request.production_readiness_evidence_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "approval request production_readiness_evidence_identity must not be empty");
    }

    if (request.requestor_identity.empty()) {
        emit_validation_error(diagnostics,
                              "approval request requestor_identity must not be empty");
    }

    if (request.approval_scope.empty()) {
        emit_validation_error(diagnostics,
                              "approval request approval_scope must not be empty");
    }

    if (request.request_justification.empty()) {
        emit_validation_error(diagnostics,
                              "approval request request_justification must not be empty");
    }

    return result;
}

ApprovalDecisionRecordValidationResult validate_approval_decision_record(
    const ApprovalDecisionRecord &decision) {
    ApprovalDecisionRecordValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (decision.format_version != kProviderApprovalDecisionFormatVersion) {
        emit_validation_error(
            diagnostics,
            "approval decision format_version must be '" +
                std::string(kProviderApprovalDecisionFormatVersion) + "'");
    }

    if (decision.approval_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "approval decision approval_request_identity must not be empty");
    }

    if (decision.decision_reason.empty()) {
        emit_validation_error(diagnostics,
                              "approval decision decision_reason must not be empty");
    }

    // 拒绝时必须有拒绝详情
    if (decision.decision == ApprovalDecision::Rejected &&
        !decision.rejection_details.has_value()) {
        emit_validation_error(
            diagnostics,
            "approval decision rejection_details required when decision is Rejected");
    }

    return result;
}

ApprovalReceiptValidationResult validate_approval_receipt(const ApprovalReceipt &receipt) {
    ApprovalReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kProviderApprovalReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "approval receipt format_version must be '" +
                std::string(kProviderApprovalReceiptFormatVersion) + "'");
    }

    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "approval receipt workflow_canonical_name and session_id must not be empty");
    }

    if (receipt.approval_request_identity.empty()) {
        emit_validation_error(diagnostics,
                              "approval receipt approval_request_identity must not be empty");
    }

    if (receipt.approval_decision_identity.empty()) {
        emit_validation_error(diagnostics,
                              "approval receipt approval_decision_identity must not be empty");
    }

    if (receipt.receipt_summary.empty()) {
        emit_validation_error(diagnostics,
                              "approval receipt receipt_summary must not be empty");
    }

    // is_approved 只能在 Approved 决定时为 true
    if (receipt.is_approved && receipt.final_decision != ApprovalDecision::Approved) {
        emit_validation_error(
            diagnostics,
            "approval receipt is_approved must be false when final_decision is not Approved");
    }

    // 非 Approved 时必须有 blocking_reason_summary
    if (receipt.final_decision != ApprovalDecision::Approved &&
        receipt.blocking_reason_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "approval receipt blocking_reason_summary required when not approved");
    }

    return result;
}

ApprovalRequestResult build_approval_request(
    const ReleaseEvidenceArchiveManifest &archive,
    const ProviderProductionReadinessEvidence &readiness) {
    ApprovalRequestResult result;

    // 验证上游 artifact 基础可用性
    result.diagnostics.append(
        validate_release_evidence_archive_manifest(archive).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.diagnostics.append(
        validate_provider_production_readiness_evidence(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ApprovalRequest request;
    request.workflow_canonical_name = archive.workflow_canonical_name;
    request.session_id = archive.session_id;
    request.run_id = archive.run_id;

    // 绑定上游 evidence identity
    request.release_evidence_archive_manifest_identity =
        "release-evidence-archive::" + archive.session_id;
    request.production_readiness_evidence_identity =
        readiness.durable_store_import_provider_production_readiness_evidence_identity;

    // 设置审批请求默认值（由组织流程决定真实值）
    request.requestor_identity = "ahfl-compiler-toolchain";
    request.approval_scope = "provider-production-integration";
    request.request_justification =
        "release evidence archive and production readiness evidence available for review";

    // 验证生成的 request
    result.diagnostics.append(validate_approval_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

ApprovalReceiptResult build_approval_receipt(
    const ApprovalRequest &request,
    const ApprovalDecisionRecord &decision) {
    ApprovalReceiptResult result;

    // 验证上游 artifact
    result.diagnostics.append(validate_approval_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.diagnostics.append(validate_approval_decision_record(decision).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ApprovalReceipt receipt;
    receipt.workflow_canonical_name = request.workflow_canonical_name;
    receipt.session_id = request.session_id;
    receipt.run_id = request.run_id;

    // 绑定上游 artifact identity
    receipt.approval_request_identity =
        "approval-request::" + request.session_id;
    receipt.approval_decision_identity =
        "approval-decision::" + decision.approval_request_identity;

    // 传递最终决定
    receipt.final_decision = decision.decision;
    // 安全默认：只在 Approved 决定时设置 is_approved = true
    receipt.is_approved = (decision.decision == ApprovalDecision::Approved);

    // Evidence binding
    receipt.release_evidence_archive_manifest_identity =
        request.release_evidence_archive_manifest_identity;
    receipt.production_readiness_evidence_identity =
        request.production_readiness_evidence_identity;

    // 生成汇总摘要
    switch (decision.decision) {
    case ApprovalDecision::Approved:
        receipt.receipt_summary =
            "production integration approved; " + decision.decision_reason;
        receipt.blocking_reason_summary = "none";
        break;
    case ApprovalDecision::Rejected:
        receipt.receipt_summary =
            "production integration rejected; " + decision.decision_reason;
        receipt.blocking_reason_summary =
            decision.rejection_details.value_or("rejection details not provided");
        break;
    case ApprovalDecision::Deferred:
        receipt.receipt_summary =
            "production integration deferred; " + decision.decision_reason;
        receipt.blocking_reason_summary =
            "deferred: " + decision.decision_reason;
        break;
    }

    // 验证生成的 receipt
    result.diagnostics.append(validate_approval_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

} // namespace ahfl::durable_store_import

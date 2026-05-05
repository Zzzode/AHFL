#include "ahfl/durable_store_import/provider_approval_workflow.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

namespace {

using namespace ahfl::durable_store_import;

// 辅助函数：创建有效的 ReleaseEvidenceArchiveManifest
[[nodiscard]] ReleaseEvidenceArchiveManifest make_archive_manifest() {
    ReleaseEvidenceArchiveManifest manifest;
    manifest.format_version = std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion);
    manifest.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    manifest.session_id = "session-001";
    manifest.run_id = "run-001";
    manifest.durable_store_import_provider_conformance_report_identity =
        "conformance-report::session-001";
    manifest.durable_store_import_provider_schema_compatibility_report_identity =
        "schema-report::session-001";
    manifest.durable_store_import_provider_config_bundle_validation_report_identity =
        "config-report::session-001";
    manifest.durable_store_import_provider_production_readiness_evidence_identity =
        "readiness-evidence::session-001";
    manifest.total_evidence_count = 4;
    manifest.present_evidence_count = 4;
    manifest.valid_evidence_count = 4;
    manifest.missing_evidence_count = 0;
    manifest.invalid_evidence_count = 0;
    manifest.is_release_ready = true;
    manifest.archive_summary = "all evidence present and valid; release ready";
    manifest.missing_evidence_summary = "none";
    manifest.stale_evidence_summary = "none";
    manifest.incompatible_evidence_summary = "none";
    return manifest;
}

// 辅助函数：创建有效的 ProviderProductionReadinessEvidence
[[nodiscard]] ProviderProductionReadinessEvidence make_readiness_evidence() {
    ProviderProductionReadinessEvidence evidence;
    evidence.format_version = std::string(kProviderProductionReadinessEvidenceFormatVersion);
    evidence.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    evidence.session_id = "session-001";
    evidence.run_id = "run-001";
    evidence.input_fixture = "fixture.request.ok";
    evidence.durable_store_import_provider_selection_plan_identity = "selection-plan::session-001";
    evidence.durable_store_import_provider_compatibility_report_identity =
        "compatibility-report::session-001";
    evidence.durable_store_import_provider_execution_audit_event_identity =
        "audit-event::session-001";
    evidence.durable_store_import_provider_write_recovery_plan_identity =
        "recovery-plan::session-001";
    evidence.durable_store_import_provider_failure_taxonomy_report_identity =
        "taxonomy-report::session-001";
    evidence.durable_store_import_provider_production_readiness_evidence_identity =
        "readiness-evidence::session-001";
    evidence.security_evidence_passed = true;
    evidence.recovery_evidence_passed = true;
    evidence.compatibility_evidence_passed = true;
    evidence.observability_evidence_passed = true;
    evidence.registry_evidence_passed = true;
    evidence.blocking_issue_count = 0;
    evidence.evidence_summary = "all readiness checks passed";
    return evidence;
}

// 测试：构建 approval request 成功
int test_build_approval_request() {
    const auto archive = make_archive_manifest();
    const auto readiness = make_readiness_evidence();

    const auto result = build_approval_request(archive, readiness);

    if (!result.request.has_value()) {
        std::cerr << "FAIL: build_approval_request should produce a request\n";
        return 1;
    }

    const auto &request = *result.request;

    assert(request.format_version == kProviderApprovalRequestFormatVersion);
    assert(request.workflow_canonical_name == "app::main::ValueFlowWorkflow");
    assert(request.session_id == "session-001");
    assert(request.run_id.has_value() && *request.run_id == "run-001");
    assert(!request.release_evidence_archive_manifest_identity.empty());
    assert(!request.production_readiness_evidence_identity.empty());
    assert(!request.requestor_identity.empty());
    assert(!request.approval_scope.empty());
    assert(!request.request_justification.empty());

    std::cout << "PASS: test_build_approval_request\n";
    return 0;
}

// 测试：构建 approval receipt（Rejected 默认）
int test_build_approval_receipt_rejected() {
    const auto archive = make_archive_manifest();
    const auto readiness = make_readiness_evidence();

    const auto request_result = build_approval_request(archive, readiness);
    assert(request_result.request.has_value());

    // 构建 Rejected 决定
    ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::session-001";
    decision.decision = ApprovalDecision::Rejected;
    decision.decision_reason = "operator has not approved";
    decision.rejection_details = "pending review";

    const auto result = build_approval_receipt(*request_result.request, decision);

    if (!result.receipt.has_value()) {
        std::cerr << "FAIL: build_approval_receipt should produce a receipt\n";
        return 1;
    }

    const auto &receipt = *result.receipt;

    // 验证安全默认：不自动批准
    assert(!receipt.is_approved);
    assert(receipt.final_decision == ApprovalDecision::Rejected);
    assert(!receipt.blocking_reason_summary.empty());
    assert(!receipt.receipt_summary.empty());

    std::cout << "PASS: test_build_approval_receipt_rejected\n";
    return 0;
}

// 测试：构建 approval receipt（Approved）
int test_build_approval_receipt_approved() {
    const auto archive = make_archive_manifest();
    const auto readiness = make_readiness_evidence();

    const auto request_result = build_approval_request(archive, readiness);
    assert(request_result.request.has_value());

    // 构建 Approved 决定
    ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::session-001";
    decision.decision = ApprovalDecision::Approved;
    decision.decision_reason = "all evidence satisfactory";
    decision.approver_identity = "operator@org.example";

    const auto result = build_approval_receipt(*request_result.request, decision);

    if (!result.receipt.has_value()) {
        std::cerr << "FAIL: build_approval_receipt (approved) should produce a receipt\n";
        return 1;
    }

    const auto &receipt = *result.receipt;

    assert(receipt.is_approved);
    assert(receipt.final_decision == ApprovalDecision::Approved);
    assert(receipt.blocking_reason_summary == "none");

    std::cout << "PASS: test_build_approval_receipt_approved\n";
    return 0;
}

// 测试：构建 approval receipt（Deferred）
int test_build_approval_receipt_deferred() {
    const auto archive = make_archive_manifest();
    const auto readiness = make_readiness_evidence();

    const auto request_result = build_approval_request(archive, readiness);
    assert(request_result.request.has_value());

    // 构建 Deferred 决定
    ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::session-001";
    decision.decision = ApprovalDecision::Deferred;
    decision.decision_reason = "additional evidence requested";

    const auto result = build_approval_receipt(*request_result.request, decision);

    if (!result.receipt.has_value()) {
        std::cerr << "FAIL: build_approval_receipt (deferred) should produce a receipt\n";
        return 1;
    }

    const auto &receipt = *result.receipt;

    assert(!receipt.is_approved);
    assert(receipt.final_decision == ApprovalDecision::Deferred);
    assert(!receipt.blocking_reason_summary.empty());

    std::cout << "PASS: test_build_approval_receipt_deferred\n";
    return 0;
}

// 测试：验证 format version 校验
int test_validate_approval_request_format_version() {
    ApprovalRequest request;
    request.format_version = "invalid-version";
    request.workflow_canonical_name = "test";
    request.session_id = "session-001";
    request.release_evidence_archive_manifest_identity = "archive-001";
    request.production_readiness_evidence_identity = "readiness-001";
    request.requestor_identity = "test";
    request.approval_scope = "test";
    request.request_justification = "test";

    const auto result = validate_approval_request(request);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect invalid format version\n";
        return 1;
    }

    std::cout << "PASS: test_validate_approval_request_format_version\n";
    return 0;
}

// 测试：验证 rejection_details 对 Rejected 决定是必需的
int test_validate_decision_rejection_details() {
    ApprovalDecisionRecord decision;
    decision.format_version = std::string(kProviderApprovalDecisionFormatVersion);
    decision.approval_request_identity = "approval-request::session-001";
    decision.decision = ApprovalDecision::Rejected;
    decision.decision_reason = "denied";
    // 不设置 rejection_details

    const auto result = validate_approval_decision_record(decision);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should require rejection_details for Rejected decision\n";
        return 1;
    }

    std::cout << "PASS: test_validate_decision_rejection_details\n";
    return 0;
}

// 测试：验证 receipt 中 is_approved 与 final_decision 一致性
int test_validate_receipt_approval_consistency() {
    ApprovalReceipt receipt;
    receipt.format_version = std::string(kProviderApprovalReceiptFormatVersion);
    receipt.workflow_canonical_name = "test";
    receipt.session_id = "session-001";
    receipt.approval_request_identity = "request-001";
    receipt.approval_decision_identity = "decision-001";
    receipt.final_decision = ApprovalDecision::Rejected;
    receipt.is_approved = true;  // 不一致
    receipt.receipt_summary = "test";
    receipt.blocking_reason_summary = "reason";

    const auto result = validate_approval_receipt(receipt);

    if (!result.has_errors()) {
        std::cerr << "FAIL: should detect is_approved inconsistent with final_decision\n";
        return 1;
    }

    std::cout << "PASS: test_validate_receipt_approval_consistency\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-build-approval-request") return test_build_approval_request();
        if (cmd == "test-build-receipt-rejected") return test_build_approval_receipt_rejected();
        if (cmd == "test-build-receipt-approved") return test_build_approval_receipt_approved();
        if (cmd == "test-build-receipt-deferred") return test_build_approval_receipt_deferred();
        if (cmd == "test-validate-format-version")
            return test_validate_approval_request_format_version();
        if (cmd == "test-validate-rejection-details")
            return test_validate_decision_rejection_details();
        if (cmd == "test-validate-receipt-consistency")
            return test_validate_receipt_approval_consistency();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    int failures = 0;
    failures += test_build_approval_request();
    failures += test_build_approval_receipt_rejected();
    failures += test_build_approval_receipt_approved();
    failures += test_build_approval_receipt_deferred();
    failures += test_validate_approval_request_format_version();
    failures += test_validate_decision_rejection_details();
    failures += test_validate_receipt_approval_consistency();

    if (failures == 0) {
        std::cout << "\nAll approval workflow tests passed.\n";
    }
    return failures;
}

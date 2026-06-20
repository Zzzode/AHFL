#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/base/support/diagnostics.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/production_readiness.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/release_evidence_archive.hpp"

namespace ahfl::durable_store_import {

// Format version constant for the Approval Request
inline constexpr std::string_view kProviderApprovalRequestFormatVersion =
    "ahfl.durable-store-import-provider-approval-request.v1";

// Format version constant for the Approval Decision
inline constexpr std::string_view kProviderApprovalDecisionFormatVersion =
    "ahfl.durable-store-import-provider-approval-decision.v1";

// Format version constant for the Approval Receipt
inline constexpr std::string_view kProviderApprovalReceiptFormatVersion =
    "ahfl.durable-store-import-provider-approval-receipt.v1";

// Approval decision enumeration
enum class ApprovalDecision {
    Approved, // Approved
    Rejected, // Rejected
    Deferred, // Deferred decision
};

// Approval request - v0.47
// Represents the human-approval input for production integration
struct ApprovalRequest {
    std::string format_version{std::string(kProviderApprovalRequestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Upstream evidence references
    std::string release_evidence_archive_manifest_identity;
    std::string production_readiness_evidence_identity;

    // Requestor identity and approval scope
    std::string requestor_identity;
    std::string approval_scope;
    std::string request_justification;
};

// Approval decision record - v0.47
// Represents the approval decision, rejection reason, and evidence chain
struct ApprovalDecisionRecord {
    std::string format_version{std::string(kProviderApprovalDecisionFormatVersion)};
    std::string approval_request_identity;
    ApprovalDecision decision{ApprovalDecision::Rejected};
    std::string decision_reason;
    std::optional<std::string> approver_identity;
    std::optional<std::string> rejection_details;
    std::optional<std::string> conditions;
};

// Approval receipt - v0.47
// Approval result surfaced to operator-facing consumers
struct ApprovalReceipt {
    std::string format_version{std::string(kProviderApprovalReceiptFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Upstream artifact references
    std::string approval_request_identity;
    std::string approval_decision_identity;

    // Final decision
    ApprovalDecision final_decision{ApprovalDecision::Rejected};

    // Evidence binding
    std::string release_evidence_archive_manifest_identity;
    std::string production_readiness_evidence_identity;

    // Aggregated summary
    std::string receipt_summary;
    std::string blocking_reason_summary;

    // Status flag (safe default: false)
    bool is_approved{false};
};

// Validation result
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

// Build result
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

// Validation functions
[[nodiscard]] ApprovalRequestValidationResult
validate_approval_request(const ApprovalRequest &request);

[[nodiscard]] ApprovalDecisionRecordValidationResult
validate_approval_decision_record(const ApprovalDecisionRecord &decision);

[[nodiscard]] ApprovalReceiptValidationResult
validate_approval_receipt(const ApprovalReceipt &receipt);

// Build functions
// Build an ApprovalRequest from a ReleaseEvidenceArchiveManifest and a ProviderProductionReadinessEvidence
[[nodiscard]] ApprovalRequestResult
build_approval_request(const ReleaseEvidenceArchiveManifest &archive,
                       const ProviderProductionReadinessEvidence &readiness);

// Build an ApprovalReceipt from an ApprovalRequest and an ApprovalDecisionRecord
[[nodiscard]] ApprovalReceiptResult build_approval_receipt(const ApprovalRequest &request,
                                                           const ApprovalDecisionRecord &decision);

// Helper: convert an ApprovalDecision to a string
[[nodiscard]] std::string_view to_string_view(ApprovalDecision decision);

} // namespace ahfl::durable_store_import

#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_RECEIPT_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "durable store import receipt review summary");
}

void validate_receipt_blocker(const ReceiptBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary receipt_blocker message must not be empty");
    }
}

[[nodiscard]] std::string capability_name(AdapterCapabilityKind capability) {
    switch (capability) {
    case AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] DurableStoreImportDecisionReceiptReviewNextActionKind
next_action_for_receipt(const DurableStoreImportDecisionReceipt &receipt) {
    switch (receipt.receipt_status) {
    case DurableStoreImportDecisionReceiptStatus::ReadyForArchive:
        return receipt.request_status == DurableStoreImportRequestStatus::TerminalCompleted
                   ? DurableStoreImportDecisionReceiptReviewNextActionKind::
                         ArchiveCompletedDurableStoreImportDecisionReceipt
                   : DurableStoreImportDecisionReceiptReviewNextActionKind::
                         HandoffDurableStoreImportDecisionReceipt;
    case DurableStoreImportDecisionReceiptStatus::Blocked:
        return DurableStoreImportDecisionReceiptReviewNextActionKind::
            ResolveRequiredAdapterCapability;
    case DurableStoreImportDecisionReceiptStatus::Deferred:
        return DurableStoreImportDecisionReceiptReviewNextActionKind::
            PreservePartialDurableStoreImportDecisionReceipt;
    case DurableStoreImportDecisionReceiptStatus::Rejected:
        return DurableStoreImportDecisionReceiptReviewNextActionKind::
            InvestigateDurableStoreImportDecisionReceiptRejection;
    }

    return DurableStoreImportDecisionReceiptReviewNextActionKind::
        ResolveRequiredAdapterCapability;
}

[[nodiscard]] std::string adapter_receipt_contract_summary_for_receipt(
    const DurableStoreImportDecisionReceipt &receipt) {
    switch (receipt.receipt_boundary_kind) {
    case ReceiptBoundaryKind::LocalContractOnly:
        return "local durable adapter receipt contract reasoning only; real receipt persistence and executor ABI not yet promised";
    case ReceiptBoundaryKind::AdapterReceiptConsumable:
        return "adapter-receipt-consumable contract shape available; real receipt persistence and executor ABI still not promised";
    }

    return "invalid durable adapter receipt boundary";
}

[[nodiscard]] std::string receipt_preview_for_receipt(
    const DurableStoreImportDecisionReceipt &receipt) {
    switch (receipt.receipt_status) {
    case DurableStoreImportDecisionReceiptStatus::ReadyForArchive:
        if (receipt.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            return "workflow already completed; durable adapter receipt is retained for archival review";
        }
        return "durable adapter receipt '" + receipt.durable_store_import_receipt_identity +
               "' is ready for future adapter archive handoff";
    case DurableStoreImportDecisionReceiptStatus::Blocked:
        return "durable adapter receipt remains blocked until required capability '" +
               (receipt.next_required_adapter_capability.has_value()
                    ? capability_name(*receipt.next_required_adapter_capability)
                    : std::string("unknown")) +
               "' is available";
    case DurableStoreImportDecisionReceiptStatus::Deferred:
        return "partial workflow durable adapter receipt is deferred until partial state preservation is available";
    case DurableStoreImportDecisionReceiptStatus::Rejected:
        return receipt.workflow_failure_summary.has_value() &&
                       receipt.workflow_failure_summary->node_name.has_value()
                   ? "workflow failed at node '" +
                         *receipt.workflow_failure_summary->node_name +
                         "'; durable adapter receipt rejects archive handoff"
                   : "workflow failure rejects durable adapter receipt archive handoff";
    }

    return "invalid durable adapter receipt preview";
}

[[nodiscard]] std::string next_step_recommendation_for_receipt(
    const DurableStoreImportDecisionReceipt &receipt) {
    switch (receipt.receipt_status) {
    case DurableStoreImportDecisionReceiptStatus::ReadyForArchive:
        if (receipt.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            return "archive completed durable adapter receipt; no further adapter action";
        }
        return "handoff current durable adapter receipt before real adapter implementation work";
    case DurableStoreImportDecisionReceiptStatus::Blocked:
        return "add required adapter capability before claiming durable adapter receipt archive readiness";
    case DurableStoreImportDecisionReceiptStatus::Deferred:
        return "preserve partial workflow state before durable adapter receipt archive planning";
    case DurableStoreImportDecisionReceiptStatus::Rejected:
        return "inspect workflow failure before planning durable adapter receipt archive";
    }

    return "invalid durable adapter receipt next step";
}

} // namespace

DurableStoreImportDecisionReceiptReviewSummaryValidationResult
validate_durable_store_import_decision_receipt_review_summary(
    const DurableStoreImportDecisionReceiptReviewSummary &summary) {
    DurableStoreImportDecisionReceiptReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kDurableStoreImportDecisionReceiptReviewSummaryFormatVersion) {
        emit_validation_error(diagnostics, "durable store import receipt review summary format_version must be '" +
                          std::string(kDurableStoreImportDecisionReceiptReviewSummaryFormatVersion) +
                          "'");
    }

    if (summary.source_durable_store_import_decision_receipt_format_version !=
        kDurableStoreImportDecisionReceiptFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary source_durable_store_import_decision_receipt_format_version must be '" +
            std::string(kDurableStoreImportDecisionReceiptFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_format_version !=
        kDurableStoreImportDecisionFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary source_durable_store_import_decision_format_version must be '" +
            std::string(kDurableStoreImportDecisionFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_request_format_version !=
        kDurableStoreImportRequestFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary source_durable_store_import_request_format_version must be '" +
            std::string(kDurableStoreImportRequestFormatVersion) + "'");
    }

    if (summary.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary workflow_canonical_name "
                          "must not be empty");
    }

    if (summary.session_id.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary run_id must not be empty "
                          "when present");
    }

    if (summary.input_fixture.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary input_fixture must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary export_package_identity "
                          "must not be empty");
    }

    if (summary.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary store_import_candidate_identity must not be empty");
    }

    if (summary.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary durable_store_import_request_identity must not be empty");
    }

    if (summary.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary durable_store_import_decision_identity must not be empty");
    }

    if (summary.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary durable_store_import_receipt_identity must not be empty");
    }

    if (summary.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.accepted_for_receipt_archive && summary.receipt_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary cannot contain "
                          "receipt_blocker when accepted_for_receipt_archive is true");
    }

    if (summary.accepted_for_receipt_archive &&
        summary.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary cannot contain "
                          "next_required_adapter_capability when accepted_for_receipt_archive "
                          "is true");
    }

    if (summary.receipt_blocker.has_value()) {
        validate_receipt_blocker(*summary.receipt_blocker, diagnostics);
    }

    if (summary.accepted_for_receipt_archive &&
        summary.receipt_boundary_kind != ReceiptBoundaryKind::AdapterReceiptConsumable) {
        emit_validation_error(diagnostics, "durable store import receipt review summary accepted receipt requires "
                          "adapter_receipt_consumable receipt_boundary_kind");
    }

    if (summary.receipt_boundary_kind == ReceiptBoundaryKind::AdapterReceiptConsumable &&
        !summary.accepted_for_receipt_archive) {
        emit_validation_error(diagnostics, "durable store import receipt review summary adapter-receipt-consumable "
                          "boundary requires accepted_for_receipt_archive");
    }

    if (summary.adapter_receipt_contract_summary.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary "
                          "adapter_receipt_contract_summary must not be empty");
    }

    if (summary.receipt_preview.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import receipt review summary receipt_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt review summary "
                          "next_step_recommendation must not be empty");
    }

    switch (summary.receipt_status) {
    case DurableStoreImportDecisionReceiptStatus::ReadyForArchive:
        if (summary.receipt_outcome !=
            DurableStoreImportDecisionReceiptOutcome::ArchiveAcceptedDecision) {
            emit_validation_error(diagnostics, "durable store import receipt review summary ReadyForArchive "
                              "receipt_status requires "
                              "ArchiveAcceptedDecision receipt_outcome");
        }
        if (summary.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            if (summary.next_action !=
                DurableStoreImportDecisionReceiptReviewNextActionKind::
                    ArchiveCompletedDurableStoreImportDecisionReceipt) {
                emit_validation_error(diagnostics, 
                    "durable store import receipt review summary completed ReadyForArchive receipt requires next_action archive_completed_durable_store_import_decision_receipt");
            }
        } else if (summary.next_action !=
                   DurableStoreImportDecisionReceiptReviewNextActionKind::
                       HandoffDurableStoreImportDecisionReceipt) {
            emit_validation_error(diagnostics, 
                "durable store import receipt review summary ReadyForArchive receipt requires next_action handoff_durable_store_import_decision_receipt");
        }
        if (!summary.accepted_for_receipt_archive) {
            emit_validation_error(diagnostics, "durable store import receipt review summary ReadyForArchive receipt "
                              "requires accepted_for_receipt_archive");
        }
        if (summary.decision_status != DurableStoreImportDecisionStatus::Accepted) {
            emit_validation_error(diagnostics, "durable store import receipt review summary ReadyForArchive receipt "
                              "requires Accepted decision_status");
        }
        break;
    case DurableStoreImportDecisionReceiptStatus::Blocked:
        if (summary.receipt_outcome !=
            DurableStoreImportDecisionReceiptOutcome::BlockBlockedDecision) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Blocked receipt_status "
                              "requires BlockBlockedDecision receipt_outcome");
        }
        if (summary.next_action !=
            DurableStoreImportDecisionReceiptReviewNextActionKind::
                ResolveRequiredAdapterCapability) {
            emit_validation_error(diagnostics, 
                "durable store import receipt review summary Blocked receipt requires next_action resolve_required_adapter_capability");
        }
        if (!summary.receipt_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Blocked receipt "
                              "requires receipt_blocker");
        }
        break;
    case DurableStoreImportDecisionReceiptStatus::Deferred:
        if (summary.receipt_outcome !=
            DurableStoreImportDecisionReceiptOutcome::DeferPartialDecision) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Deferred receipt_status "
                              "requires DeferPartialDecision receipt_outcome");
        }
        if (summary.next_action !=
            DurableStoreImportDecisionReceiptReviewNextActionKind::
                PreservePartialDurableStoreImportDecisionReceipt) {
            emit_validation_error(diagnostics, 
                "durable store import receipt review summary Deferred receipt requires next_action preserve_partial_durable_store_import_decision_receipt");
        }
        if (!summary.receipt_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Deferred receipt "
                              "requires receipt_blocker");
        }
        break;
    case DurableStoreImportDecisionReceiptStatus::Rejected:
        if (summary.receipt_outcome !=
            DurableStoreImportDecisionReceiptOutcome::RejectFailedDecision) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Rejected receipt_status "
                              "requires RejectFailedDecision receipt_outcome");
        }
        if (summary.next_action !=
            DurableStoreImportDecisionReceiptReviewNextActionKind::
                InvestigateDurableStoreImportDecisionReceiptRejection) {
            emit_validation_error(diagnostics, 
                "durable store import receipt review summary Rejected receipt requires next_action investigate_durable_store_import_decision_receipt_rejection");
        }
        if (!summary.receipt_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import receipt review summary Rejected receipt "
                              "requires receipt_blocker");
        }
        break;
    }

    return result;
}

DurableStoreImportDecisionReceiptReviewSummaryResult
build_durable_store_import_decision_receipt_review_summary(
    const DurableStoreImportDecisionReceipt &receipt) {
    DurableStoreImportDecisionReceiptReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_durable_store_import_decision_receipt(receipt);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    DurableStoreImportDecisionReceiptReviewSummary summary{
        .format_version =
            std::string(kDurableStoreImportDecisionReceiptReviewSummaryFormatVersion),
        .source_durable_store_import_decision_receipt_format_version = receipt.format_version,
        .source_durable_store_import_decision_format_version =
            receipt.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            receipt.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            receipt.source_store_import_descriptor_format_version,
        .workflow_canonical_name = receipt.workflow_canonical_name,
        .session_id = receipt.session_id,
        .run_id = receipt.run_id,
        .input_fixture = receipt.input_fixture,
        .workflow_status = receipt.workflow_status,
        .checkpoint_status = receipt.checkpoint_status,
        .persistence_status = receipt.persistence_status,
        .manifest_status = receipt.manifest_status,
        .descriptor_status = receipt.descriptor_status,
        .request_status = receipt.request_status,
        .decision_status = receipt.decision_status,
        .receipt_status = receipt.receipt_status,
        .workflow_failure_summary = receipt.workflow_failure_summary,
        .export_package_identity = receipt.export_package_identity,
        .store_import_candidate_identity = receipt.store_import_candidate_identity,
        .durable_store_import_request_identity =
            receipt.durable_store_import_request_identity,
        .durable_store_import_decision_identity =
            receipt.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity =
            receipt.durable_store_import_receipt_identity,
        .planned_durable_identity = receipt.planned_durable_identity,
        .receipt_boundary_kind = receipt.receipt_boundary_kind,
        .receipt_outcome = receipt.receipt_outcome,
        .accepted_for_receipt_archive = receipt.accepted_for_receipt_archive,
        .next_required_adapter_capability = receipt.next_required_adapter_capability,
        .receipt_blocker = receipt.receipt_blocker,
        .next_action = next_action_for_receipt(receipt),
        .adapter_receipt_contract_summary =
            adapter_receipt_contract_summary_for_receipt(receipt),
        .receipt_preview = receipt_preview_for_receipt(receipt),
        .next_step_recommendation = next_step_recommendation_for_receipt(receipt),
    };

    const auto summary_validation =
        validate_durable_store_import_decision_receipt_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::durable_store_import

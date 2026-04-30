#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_RECEIPT_PERSISTENCE_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary,
        owner_name,
        diagnostics,
        "durable store import receipt persistence review summary");
}

void validate_receipt_persistence_blocker(const PersistenceBlocker &blocker,
                                          DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "receipt_persistence_blocker message must not be empty");
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

[[nodiscard]] PersistenceReviewNextActionKind
next_action_for_request(const PersistenceRequest &request) {
    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        return request.request_status == RequestStatus::TerminalCompleted
                   ? PersistenceReviewNextActionKind::
                         PersistCompletedDurableStoreImportDecisionReceipt
                   : PersistenceReviewNextActionKind::
                         HandoffDurableStoreImportDecisionReceiptPersistenceRequest;
    case PersistenceRequestStatus::Blocked:
        return PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability;
    case PersistenceRequestStatus::Deferred:
        return PersistenceReviewNextActionKind::PreservePartialDurableStoreImportDecisionReceipt;
    case PersistenceRequestStatus::Rejected:
        return PersistenceReviewNextActionKind::
            InvestigateDurableStoreImportDecisionReceiptPersistenceRejection;
    }

    return PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability;
}

[[nodiscard]] std::string
adapter_receipt_persistence_contract_summary_for_request(const PersistenceRequest &request) {
    switch (request.receipt_persistence_boundary_kind) {
    case PersistenceBoundaryKind::LocalContractOnly:
        return "local durable adapter receipt persistence contract reasoning only; real receipt "
               "persistence writer and recovery ABI not yet promised";
    case PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable:
        return "adapter-receipt-persistence-consumable contract shape available; real receipt "
               "persistence writer and recovery ABI still not promised";
    }

    return "invalid durable adapter receipt persistence boundary";
}

[[nodiscard]] std::string persistence_preview_for_request(const PersistenceRequest &request) {
    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        if (request.request_status == RequestStatus::TerminalCompleted) {
            return "workflow already completed; durable adapter receipt persistence request is "
                   "retained for archival review";
        }
        return "durable adapter receipt persistence request '" +
               request.durable_store_import_receipt_persistence_request_identity +
               "' is ready for future adapter receipt persistence handoff";
    case PersistenceRequestStatus::Blocked:
        return "durable adapter receipt persistence request remains blocked until required "
               "capability '" +
               (request.next_required_adapter_capability.has_value()
                    ? capability_name(*request.next_required_adapter_capability)
                    : std::string("unknown")) +
               "' is available";
    case PersistenceRequestStatus::Deferred:
        return "partial workflow durable adapter receipt persistence request is deferred until "
               "partial state preservation is available";
    case PersistenceRequestStatus::Rejected:
        return request.workflow_failure_summary.has_value() &&
                       request.workflow_failure_summary->node_name.has_value()
                   ? "workflow failed at node '" + *request.workflow_failure_summary->node_name +
                         "'; durable adapter receipt persistence request rejects handoff"
                   : "workflow failure rejects durable adapter receipt persistence handoff";
    }

    return "invalid durable adapter receipt persistence preview";
}

[[nodiscard]] std::string next_step_recommendation_for_request(const PersistenceRequest &request) {
    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        if (request.request_status == RequestStatus::TerminalCompleted) {
            return "archive completed durable adapter receipt persistence request; no further "
                   "adapter action";
        }
        return "handoff current durable adapter receipt persistence request before real adapter "
               "implementation work";
    case PersistenceRequestStatus::Blocked:
        return "add required adapter capability before claiming durable adapter receipt "
               "persistence readiness";
    case PersistenceRequestStatus::Deferred:
        return "preserve partial workflow state before durable adapter receipt persistence "
               "planning";
    case PersistenceRequestStatus::Rejected:
        return "inspect workflow failure before planning durable adapter receipt persistence";
    }

    return "invalid durable adapter receipt persistence next step";
}

} // namespace

PersistenceReviewSummaryValidationResult
validate_persistence_review_summary(const PersistenceReviewSummary &summary) {
    PersistenceReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kPersistenceReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary format_version must be '" +
                std::string(kPersistenceReviewFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_receipt_persistence_request_format_version !=
        kPersistenceRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "source_durable_store_import_decision_receipt_persistence_request_"
                              "format_version must be '" +
                                  std::string(kPersistenceRequestFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_receipt_format_version !=
        kReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary "
            "source_durable_store_import_decision_receipt_format_version must be '" +
                std::string(kReceiptFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_format_version != kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "source_durable_store_import_decision_format_version must be '" +
                                  std::string(kDecisionFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_request_format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "source_durable_store_import_request_format_version must be '" +
                                  std::string(kRequestFormatVersion) + "'");
    }

    if (summary.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "source_store_import_descriptor_format_version must be '" +
                                  std::string(store_import::kStoreImportDescriptorFormatVersion) +
                                  "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "workflow_canonical_name must not be empty");
    }

    if (summary.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary session_id must "
            "not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary run_id must "
                              "not be empty when present");
    }

    if (summary.input_fixture.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary input_fixture "
            "must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "export_package_identity must not be empty");
    }

    if (summary.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "store_import_candidate_identity must not be empty");
    }

    if (summary.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "durable_store_import_request_identity must not be empty");
    }

    if (summary.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "durable_store_import_decision_identity must not be empty");
    }

    if (summary.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "durable_store_import_receipt_identity must not be empty");
    }

    if (summary.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary "
            "durable_store_import_receipt_persistence_request_identity must not be "
            "empty");
    }

    if (summary.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.accepted_for_receipt_persistence &&
        summary.receipt_persistence_blocker.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary cannot contain "
            "receipt_persistence_blocker when "
            "accepted_for_receipt_persistence is true");
    }

    if (summary.accepted_for_receipt_persistence &&
        summary.next_required_adapter_capability.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary cannot contain "
            "next_required_adapter_capability when "
            "accepted_for_receipt_persistence is true");
    }

    if (!summary.accepted_for_receipt_persistence &&
        !summary.receipt_persistence_blocker.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence review summary must contain "
            "receipt_persistence_blocker when "
            "accepted_for_receipt_persistence is false");
    }

    if (summary.receipt_persistence_blocker.has_value()) {
        validate_receipt_persistence_blocker(*summary.receipt_persistence_blocker, diagnostics);
    }

    if (summary.accepted_for_receipt_persistence &&
        summary.receipt_persistence_boundary_kind !=
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary accepted "
                              "request requires adapter_receipt_persistence_consumable "
                              "receipt_persistence_boundary_kind");
    }

    if (summary.receipt_persistence_boundary_kind ==
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable &&
        !summary.accepted_for_receipt_persistence) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "adapter-receipt-persistence-consumable boundary requires "
                              "accepted_for_receipt_persistence");
    }

    if (summary.adapter_receipt_persistence_contract_summary.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "adapter_receipt_persistence_contract_summary must not be empty");
    }

    if (summary.persistence_preview.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "persistence_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence review summary "
                              "next_step_recommendation must not be empty");
    }

    switch (summary.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        if (summary.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::PersistReadyReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "ReadyToPersist status requires PersistReadyReceipt outcome");
        }
        if (summary.request_status == RequestStatus::TerminalCompleted) {
            if (summary.next_action != PersistenceReviewNextActionKind::
                                           PersistCompletedDurableStoreImportDecisionReceipt) {
                emit_validation_error(diagnostics,
                                      "durable store import receipt persistence review summary "
                                      "completed ReadyToPersist status requires next_action "
                                      "persist_completed_durable_store_import_decision_receipt");
            }
        } else if (summary.next_action !=
                   PersistenceReviewNextActionKind::
                       HandoffDurableStoreImportDecisionReceiptPersistenceRequest) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence review summary ReadyToPersist status "
                "requires next_action "
                "handoff_durable_store_import_decision_receipt_persistence_request");
        }
        if (!summary.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "ReadyToPersist status requires "
                                  "accepted_for_receipt_persistence");
        }
        if (summary.decision_status != DecisionStatus::Accepted) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "ReadyToPersist status requires Accepted decision_status");
        }
        if (summary.receipt_status != ReceiptStatus::ReadyForArchive) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "ReadyToPersist status requires ReadyForArchive receipt_status");
        }
        break;
    case PersistenceRequestStatus::Blocked:
        if (summary.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::BlockBlockedReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Blocked status requires BlockBlockedReceipt outcome");
        }
        if (summary.next_action !=
            PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence review summary Blocked status requires "
                "next_action resolve_required_adapter_capability");
        }
        if (summary.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Blocked status cannot be accepted_for_receipt_persistence");
        }
        if (!summary.receipt_persistence_blocker.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Blocked status requires receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Deferred:
        if (summary.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::DeferPartialReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Deferred status requires DeferPartialReceipt outcome");
        }
        if (summary.next_action !=
            PersistenceReviewNextActionKind::PreservePartialDurableStoreImportDecisionReceipt) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence review summary Deferred status requires "
                "next_action preserve_partial_durable_store_import_decision_receipt");
        }
        if (summary.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Deferred status cannot be accepted_for_receipt_persistence");
        }
        if (!summary.receipt_persistence_blocker.has_value() ||
            summary.receipt_persistence_blocker->kind !=
                PersistenceBlockerKind::PartialWorkflowState) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Deferred status requires PartialWorkflowState "
                                  "receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Rejected:
        if (summary.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::RejectFailedReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Rejected status requires RejectFailedReceipt outcome");
        }
        if (summary.next_action !=
            PersistenceReviewNextActionKind::
                InvestigateDurableStoreImportDecisionReceiptPersistenceRejection) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence review summary Rejected status requires "
                "next_action "
                "investigate_durable_store_import_decision_receipt_persistence_rejection");
        }
        if (summary.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Rejected status cannot be accepted_for_receipt_persistence");
        }
        if (!summary.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Rejected status requires workflow_failure_summary");
        }
        if (!summary.receipt_persistence_blocker.has_value() ||
            summary.receipt_persistence_blocker->kind != PersistenceBlockerKind::WorkflowFailure) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence review summary "
                                  "Rejected status requires WorkflowFailure "
                                  "receipt_persistence_blocker");
        }
        break;
    }

    return result;
}

PersistenceReviewSummaryResult build_persistence_review_summary(const PersistenceRequest &request) {
    PersistenceReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_persistence_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    PersistenceReviewSummary summary{
        .format_version = std::string(kPersistenceReviewFormatVersion),
        .source_durable_store_import_decision_receipt_persistence_request_format_version =
            request.format_version,
        .source_durable_store_import_decision_receipt_format_version =
            request.source_durable_store_import_decision_receipt_format_version,
        .source_durable_store_import_decision_format_version =
            request.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            request.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            request.source_store_import_descriptor_format_version,
        .workflow_canonical_name = request.workflow_canonical_name,
        .session_id = request.session_id,
        .run_id = request.run_id,
        .input_fixture = request.input_fixture,
        .workflow_status = request.workflow_status,
        .checkpoint_status = request.checkpoint_status,
        .persistence_status = request.persistence_status,
        .manifest_status = request.manifest_status,
        .descriptor_status = request.descriptor_status,
        .request_status = request.request_status,
        .decision_status = request.decision_status,
        .receipt_status = request.receipt_status,
        .receipt_persistence_request_status = request.receipt_persistence_request_status,
        .receipt_persistence_request_outcome = request.receipt_persistence_request_outcome,
        .workflow_failure_summary = request.workflow_failure_summary,
        .export_package_identity = request.export_package_identity,
        .store_import_candidate_identity = request.store_import_candidate_identity,
        .durable_store_import_request_identity = request.durable_store_import_request_identity,
        .durable_store_import_decision_identity = request.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity = request.durable_store_import_receipt_identity,
        .durable_store_import_receipt_persistence_request_identity =
            request.durable_store_import_receipt_persistence_request_identity,
        .planned_durable_identity = request.planned_durable_identity,
        .receipt_boundary_kind = request.receipt_boundary_kind,
        .receipt_persistence_boundary_kind = request.receipt_persistence_boundary_kind,
        .accepted_for_receipt_persistence = request.accepted_for_receipt_persistence,
        .next_required_adapter_capability = request.next_required_adapter_capability,
        .receipt_persistence_blocker = request.receipt_persistence_blocker,
        .next_action = next_action_for_request(request),
        .adapter_receipt_persistence_contract_summary =
            adapter_receipt_persistence_contract_summary_for_request(request),
        .persistence_preview = persistence_preview_for_request(request),
        .next_step_recommendation = next_step_recommendation_for_request(request),
    };

    const auto summary_validation = validate_persistence_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::durable_store_import

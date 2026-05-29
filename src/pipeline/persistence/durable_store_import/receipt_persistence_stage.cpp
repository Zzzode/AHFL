#include "pipeline/persistence/durable_store_import/receipt_persistence_stage.hpp"

#include <string>

namespace ahfl::durable_store_import {

namespace {

[[nodiscard]] std::string
identity_anchor(const std::optional<handoff::PackageIdentity> &source_package_identity,
                std::string_view workflow_canonical_name) {
    if (source_package_identity.has_value()) {
        return source_package_identity->name;
    }
    return std::string(workflow_canonical_name);
}

[[nodiscard]] PersistenceBlockerKind to_persistence_blocker_kind(ReceiptBlockerKind kind) {
    switch (kind) {
    case ReceiptBlockerKind::SourceDecisionBlocked:
        return PersistenceBlockerKind::SourceReceiptBlocked;
    case ReceiptBlockerKind::MissingRequiredAdapterCapability:
        return PersistenceBlockerKind::MissingRequiredAdapterCapability;
    case ReceiptBlockerKind::WorkflowFailure:
        return PersistenceBlockerKind::WorkflowFailure;
    case ReceiptBlockerKind::PartialWorkflowState:
        return PersistenceBlockerKind::PartialWorkflowState;
    }

    return PersistenceBlockerKind::SourceReceiptBlocked;
}

[[nodiscard]] PersistenceResponseBlockerKind to_response_blocker_kind(PersistenceBlockerKind kind) {
    switch (kind) {
    case PersistenceBlockerKind::SourceReceiptBlocked:
        return PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked;
    case PersistenceBlockerKind::MissingRequiredAdapterCapability:
        return PersistenceResponseBlockerKind::MissingRequiredAdapterCapability;
    case PersistenceBlockerKind::WorkflowFailure:
        return PersistenceResponseBlockerKind::PersistenceWorkflowFailure;
    case PersistenceBlockerKind::PartialWorkflowState:
        return PersistenceResponseBlockerKind::PartialPersistenceState;
    }

    return PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked;
}

[[nodiscard]] std::string persistence_request_outcome_slug(PersistenceRequestOutcome outcome) {
    switch (outcome) {
    case PersistenceRequestOutcome::PersistReadyReceipt:
        return "persistence-ready";
    case PersistenceRequestOutcome::BlockBlockedReceipt:
        return "blocked";
    case PersistenceRequestOutcome::DeferPartialReceipt:
        return "deferred";
    case PersistenceRequestOutcome::RejectFailedReceipt:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string persistence_response_outcome_slug(PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accepted";
    case PersistenceResponseOutcome::BlockBlockedRequest:
        return "blocked";
    case PersistenceResponseOutcome::DeferDeferredRequest:
        return "deferred";
    case PersistenceResponseOutcome::RejectFailedRequest:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string build_persistence_request_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    PersistenceRequestOutcome outcome) {
    return "durable-store-import-receipt-persistence-request::" +
           identity_anchor(source_package_identity, workflow_canonical_name) +
           "::" + std::string(session_id) + "::" + persistence_request_outcome_slug(outcome);
}

[[nodiscard]] std::string build_persistence_response_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    PersistenceResponseOutcome outcome) {
    return "durable-store-import-receipt-persistence-response::" +
           identity_anchor(source_package_identity, workflow_canonical_name) +
           "::" + std::string(session_id) + "::" + persistence_response_outcome_slug(outcome);
}

void apply_receipt_status_transition(const Receipt &receipt, PersistenceRequest &request) {
    if (receipt.receipt_blocker.has_value()) {
        request.receipt_persistence_blocker = PersistenceBlocker{
            .kind = to_persistence_blocker_kind(receipt.receipt_blocker->kind),
            .message = receipt.receipt_blocker->message,
            .required_capability = receipt.receipt_blocker->required_capability,
        };
    }

    switch (receipt.receipt_status) {
    case ReceiptStatus::ReadyForArchive:
        request.receipt_persistence_request_status = PersistenceRequestStatus::ReadyToPersist;
        request.receipt_persistence_request_outcome =
            PersistenceRequestOutcome::PersistReadyReceipt;
        request.receipt_persistence_boundary_kind =
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable;
        request.accepted_for_receipt_persistence = true;
        request.next_required_adapter_capability.reset();
        request.receipt_persistence_blocker.reset();
        break;
    case ReceiptStatus::Blocked:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Blocked;
        request.receipt_persistence_request_outcome =
            PersistenceRequestOutcome::BlockBlockedReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case ReceiptStatus::Deferred:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Deferred;
        request.receipt_persistence_request_outcome =
            PersistenceRequestOutcome::DeferPartialReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case ReceiptStatus::Rejected:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Rejected;
        request.receipt_persistence_request_outcome =
            PersistenceRequestOutcome::RejectFailedReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    }
}

void apply_persistence_request_transition(const PersistenceRequest &request,
                                          PersistenceResponse &response) {
    if (request.receipt_persistence_blocker.has_value()) {
        response.response_blocker = PersistenceResponseBlocker{
            .kind = to_response_blocker_kind(request.receipt_persistence_blocker->kind),
            .message = request.receipt_persistence_blocker->message,
            .required_capability = request.receipt_persistence_blocker->required_capability,
        };
    }

    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        response.response_status = PersistenceResponseStatus::Accepted;
        response.response_outcome = PersistenceResponseOutcome::AcceptPersistenceRequest;
        response.receipt_persistence_response_boundary_kind =
            PersistenceResponseBoundaryKind::AdapterResponseConsumable;
        response.acknowledged_for_response = true;
        response.next_required_adapter_capability.reset();
        response.response_blocker.reset();
        break;
    case PersistenceRequestStatus::Blocked:
        response.response_status = PersistenceResponseStatus::Blocked;
        response.response_outcome = PersistenceResponseOutcome::BlockBlockedRequest;
        response.receipt_persistence_response_boundary_kind =
            PersistenceResponseBoundaryKind::LocalContractOnly;
        response.acknowledged_for_response = false;
        break;
    case PersistenceRequestStatus::Deferred:
        response.response_status = PersistenceResponseStatus::Deferred;
        response.response_outcome = PersistenceResponseOutcome::DeferDeferredRequest;
        response.receipt_persistence_response_boundary_kind =
            PersistenceResponseBoundaryKind::LocalContractOnly;
        response.acknowledged_for_response = false;
        break;
    case PersistenceRequestStatus::Rejected:
        response.response_status = PersistenceResponseStatus::Rejected;
        response.response_outcome = PersistenceResponseOutcome::RejectFailedRequest;
        response.receipt_persistence_response_boundary_kind =
            PersistenceResponseBoundaryKind::LocalContractOnly;
        response.acknowledged_for_response = false;
        break;
    }
}

} // namespace

PersistenceRequest build_receipt_persistence_request_projection(const Receipt &receipt) {
    PersistenceRequest request{
        .format_version = std::string(kPersistenceRequestFormatVersion),
        .source_durable_store_import_decision_receipt_format_version = receipt.format_version,
        .source_durable_store_import_decision_format_version =
            receipt.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            receipt.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            receipt.source_store_import_descriptor_format_version,
        .source_execution_plan_format_version = receipt.source_execution_plan_format_version,
        .source_runtime_session_format_version = receipt.source_runtime_session_format_version,
        .source_execution_journal_format_version = receipt.source_execution_journal_format_version,
        .source_replay_view_format_version = receipt.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            receipt.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version = receipt.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            receipt.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version = receipt.source_export_manifest_format_version,
        .source_package_identity = receipt.source_package_identity,
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
        .durable_store_import_request_identity = receipt.durable_store_import_request_identity,
        .durable_store_import_decision_identity = receipt.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity = receipt.durable_store_import_receipt_identity,
        .durable_store_import_receipt_persistence_request_identity = "",
        .planned_durable_identity = receipt.planned_durable_identity,
        .receipt_boundary_kind = receipt.receipt_boundary_kind,
        .receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly,
        .receipt_persistence_request_status = PersistenceRequestStatus::Blocked,
        .receipt_persistence_request_outcome = PersistenceRequestOutcome::BlockBlockedReceipt,
        .accepted_for_receipt_persistence = false,
        .next_required_adapter_capability = receipt.next_required_adapter_capability,
        .receipt_persistence_blocker = std::nullopt,
    };

    apply_receipt_status_transition(receipt, request);

    request.durable_store_import_receipt_persistence_request_identity =
        build_persistence_request_identity(request.source_package_identity,
                                           request.workflow_canonical_name,
                                           request.session_id,
                                           request.receipt_persistence_request_outcome);

    return request;
}

PersistenceResponse
build_receipt_persistence_response_projection(const PersistenceRequest &request) {
    PersistenceResponse response{
        .format_version = std::string(kPersistenceResponseFormatVersion),
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
        .source_execution_plan_format_version = request.source_execution_plan_format_version,
        .source_runtime_session_format_version = request.source_runtime_session_format_version,
        .source_execution_journal_format_version = request.source_execution_journal_format_version,
        .source_replay_view_format_version = request.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            request.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version = request.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            request.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version = request.source_export_manifest_format_version,
        .source_package_identity = request.source_package_identity,
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
        .persistence_request_status = request.receipt_persistence_request_status,
        .workflow_failure_summary = request.workflow_failure_summary,
        .export_package_identity = request.export_package_identity,
        .store_import_candidate_identity = request.store_import_candidate_identity,
        .durable_store_import_request_identity = request.durable_store_import_request_identity,
        .durable_store_import_decision_identity = request.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity = request.durable_store_import_receipt_identity,
        .durable_store_import_receipt_persistence_request_identity =
            request.durable_store_import_receipt_persistence_request_identity,
        .durable_store_import_receipt_persistence_response_identity = "",
        .planned_durable_identity = request.planned_durable_identity,
        .receipt_boundary_kind = request.receipt_boundary_kind,
        .receipt_persistence_boundary_kind = request.receipt_persistence_boundary_kind,
        .receipt_persistence_response_boundary_kind =
            PersistenceResponseBoundaryKind::LocalContractOnly,
        .response_status = PersistenceResponseStatus::Blocked,
        .response_outcome = PersistenceResponseOutcome::BlockBlockedRequest,
        .acknowledged_for_response = false,
        .next_required_adapter_capability = request.next_required_adapter_capability,
        .response_blocker = std::nullopt,
    };

    apply_persistence_request_transition(request, response);

    response.durable_store_import_receipt_persistence_response_identity =
        build_persistence_response_identity(response.source_package_identity,
                                            response.workflow_canonical_name,
                                            response.session_id,
                                            response.response_outcome);

    return response;
}

} // namespace ahfl::durable_store_import

#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_RECEIPT_PERSISTENCE";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(
        identity, diagnostics, "durable store import receipt persistence request");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(
        summary, diagnostics, "durable store import receipt persistence request");
}

void validate_persistence_blocker(
    const PersistenceRequest &request,
    DiagnosticBag &diagnostics) {
    if (!request.receipt_persistence_blocker.has_value()) {
        return;
    }

    const auto &blocker = *request.receipt_persistence_blocker;
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "receipt_persistence_blocker message must not be empty");
    }

    if (blocker.required_capability.has_value() &&
        request.next_required_adapter_capability != blocker.required_capability) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "receipt_persistence_blocker required_capability must match "
                          "next_required_adapter_capability");
    }
}

[[nodiscard]] PersistenceBlockerKind
to_persistence_blocker_kind(ReceiptBlockerKind kind) {
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

[[nodiscard]] std::string
persistence_outcome_slug(PersistenceRequestOutcome outcome) {
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

[[nodiscard]] std::string build_persistence_request_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    PersistenceRequestOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import-receipt-persistence-request::" + identity_anchor + "::" +
           std::string(session_id) + "::" + persistence_outcome_slug(outcome);
}

} // namespace

PersistenceRequestValidationResult
validate_persistence_request(const PersistenceRequest &request) {
    PersistenceRequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (request.format_version != kPersistenceRequestFormatVersion) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request format_version must be '" +
                          std::string(kPersistenceRequestFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_receipt_format_version !=
        kReceiptFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_durable_store_import_decision_receipt_format_version must be '" +
            std::string(kReceiptFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_format_version !=
        kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_durable_store_import_decision_format_version must be '" +
            std::string(kDecisionFormatVersion) + "'");
    }

    if (request.source_durable_store_import_request_format_version !=
        kRequestFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_durable_store_import_request_format_version must be '" +
            std::string(kRequestFormatVersion) + "'");
    }

    if (request.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (request.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (request.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (request.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (request.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (request.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (request.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (request.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (request.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request workflow_canonical_name "
                          "must not be empty");
    }

    if (request.session_id.empty()) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request session_id must not be empty");
    }

    if (request.run_id.has_value() && request.run_id->empty()) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request run_id must not be empty when present");
    }

    if (request.input_fixture.empty()) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence request input_fixture must not be empty");
    }

    if (request.export_package_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request export_package_identity "
                          "must not be empty");
    }

    if (request.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "store_import_candidate_identity must not be empty");
    }

    if (request.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "durable_store_import_request_identity must not be empty");
    }

    if (request.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "durable_store_import_decision_identity must not be empty");
    }

    if (request.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "durable_store_import_receipt_identity must not be empty");
    }

    if (request.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "durable_store_import_receipt_persistence_request_identity must not be empty");
    }

    if (request.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request planned_durable_identity "
                          "must not be empty");
    }

    if (request.source_package_identity.has_value()) {
        validate_package_identity(*request.source_package_identity, diagnostics);
    }

    if (request.workflow_failure_summary.has_value()) {
        validate_failure_summary(*request.workflow_failure_summary, diagnostics);
    }

    if (request.accepted_for_receipt_persistence &&
        request.receipt_persistence_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request cannot contain "
                          "receipt_persistence_blocker when accepted_for_receipt_persistence is true");
    }

    if (request.accepted_for_receipt_persistence &&
        request.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request cannot contain "
                          "next_required_adapter_capability when "
                          "accepted_for_receipt_persistence is true");
    }

    if (!request.accepted_for_receipt_persistence &&
        !request.receipt_persistence_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request must contain "
                          "receipt_persistence_blocker when accepted_for_receipt_persistence is false");
    }

    validate_persistence_blocker(request, diagnostics);

    if (request.accepted_for_receipt_persistence &&
        request.receipt_persistence_boundary_kind !=
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "accepted_for_receipt_persistence requires "
                          "AdapterReceiptPersistenceConsumable receipt_persistence_boundary_kind");
    }

    if (request.receipt_persistence_boundary_kind ==
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable &&
        !request.accepted_for_receipt_persistence) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request "
                          "adapter-receipt-persistence-consumable boundary requires "
                          "accepted_for_receipt_persistence");
    }

    if (request.accepted_for_receipt_persistence &&
        request.receipt_boundary_kind != ReceiptBoundaryKind::AdapterReceiptConsumable) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request accepted persistence "
                          "requires AdapterReceiptConsumable receipt_boundary_kind");
    }

    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::PersistReadyReceipt) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request ReadyToPersist "
                              "status requires PersistReadyReceipt outcome");
        }
        if (!request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request ReadyToPersist "
                              "status requires accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::ReadyForArchive) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request ReadyToPersist "
                              "status requires ReadyForArchive receipt_status");
        }
        if (request.decision_status != DecisionStatus::Accepted) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request ReadyToPersist "
                              "status requires Accepted decision_status");
        }
        if (request.request_status != RequestStatus::ReadyForAdapter &&
            request.request_status != RequestStatus::TerminalCompleted) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request ReadyToPersist "
                              "status requires ReadyForAdapter or TerminalCompleted request_status");
        }
        break;
    case PersistenceRequestStatus::Blocked:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::BlockBlockedReceipt) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "requires BlockBlockedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Blocked) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "requires Blocked receipt_status");
        }
        if (request.decision_status != DecisionStatus::Blocked) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "requires Blocked decision_status");
        }
        if (request.request_status != RequestStatus::Blocked) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "requires Blocked request_status");
        }
        if (!request.receipt_persistence_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Blocked status "
                              "requires receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Deferred:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::DeferPartialReceipt) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires DeferPartialReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Deferred) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires Deferred receipt_status");
        }
        if (request.decision_status != DecisionStatus::Deferred) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires Deferred decision_status");
        }
        if (request.request_status != RequestStatus::TerminalPartial) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires TerminalPartial request_status");
        }
        if (!request.next_required_adapter_capability.has_value() ||
            *request.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires PreservePartialWorkflowState "
                              "next_required_adapter_capability");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind !=
                PersistenceBlockerKind::PartialWorkflowState) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Deferred status "
                              "requires PartialWorkflowState receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Rejected:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::RejectFailedReceipt) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires RejectFailedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Rejected) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires Rejected receipt_status");
        }
        if (request.decision_status != DecisionStatus::Rejected) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires Rejected decision_status");
        }
        if (request.request_status != RequestStatus::TerminalFailed) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires TerminalFailed request_status");
        }
        if (!request.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires workflow_failure_summary");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind !=
                PersistenceBlockerKind::WorkflowFailure) {
            emit_validation_error(diagnostics, "durable store import receipt persistence request Rejected status "
                              "requires WorkflowFailure receipt_persistence_blocker");
        }
        break;
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        request.receipt_persistence_request_status !=
            PersistenceRequestStatus::ReadyToPersist) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request completed "
                          "workflow_status requires ReadyToPersist "
                          "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        request.receipt_persistence_request_status !=
            PersistenceRequestStatus::Rejected) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request failed "
                          "workflow_status requires Rejected "
                          "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        request.receipt_persistence_request_status !=
            PersistenceRequestStatus::ReadyToPersist &&
        request.receipt_persistence_request_status !=
            PersistenceRequestStatus::Blocked &&
        request.receipt_persistence_request_status !=
            PersistenceRequestStatus::Deferred) {
        emit_validation_error(diagnostics, "durable store import receipt persistence request partial "
                          "workflow_status must map to ReadyToPersist, Blocked, or Deferred "
                          "receipt_persistence_request_status");
    }

    return result;
}

PersistenceRequestResult
build_persistence_request(const Receipt &receipt) {
    PersistenceRequestResult result{
        .request = std::nullopt,
        .diagnostics = {},
    };

    const auto receipt_validation = validate_receipt(receipt);
    result.diagnostics.append(receipt_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

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
        .source_runtime_session_format_version =
            receipt.source_runtime_session_format_version,
        .source_execution_journal_format_version =
            receipt.source_execution_journal_format_version,
        .source_replay_view_format_version = receipt.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            receipt.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version =
            receipt.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            receipt.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version =
            receipt.source_export_manifest_format_version,
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
        .durable_store_import_request_identity =
            receipt.durable_store_import_request_identity,
        .durable_store_import_decision_identity =
            receipt.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity =
            receipt.durable_store_import_receipt_identity,
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
        request.receipt_persistence_request_outcome = PersistenceRequestOutcome::PersistReadyReceipt;
        request.receipt_persistence_boundary_kind =
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable;
        request.accepted_for_receipt_persistence = true;
        request.next_required_adapter_capability.reset();
        request.receipt_persistence_blocker.reset();
        break;
    case ReceiptStatus::Blocked:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Blocked;
        request.receipt_persistence_request_outcome = PersistenceRequestOutcome::BlockBlockedReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case ReceiptStatus::Deferred:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Deferred;
        request.receipt_persistence_request_outcome = PersistenceRequestOutcome::DeferPartialReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case ReceiptStatus::Rejected:
        request.receipt_persistence_request_status = PersistenceRequestStatus::Rejected;
        request.receipt_persistence_request_outcome = PersistenceRequestOutcome::RejectFailedReceipt;
        request.receipt_persistence_boundary_kind = PersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    }

    request.durable_store_import_receipt_persistence_request_identity =
        build_persistence_request_identity(
            request.source_package_identity,
            request.workflow_canonical_name,
            request.session_id,
            request.receipt_persistence_request_outcome);

    const auto validation = validate_persistence_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

} // namespace ahfl::durable_store_import

#include "durable_store_import/receipt_persistence.hpp"
#include "durable_store_import/receipt_persistence_stage.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> kValidationDiagnosticCode{"DSI_RECEIPT_PERSISTENCE"};

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

void validate_persistence_blocker(const PersistenceRequest &request, DiagnosticBag &diagnostics) {
    if (!request.receipt_persistence_blocker.has_value()) {
        return;
    }

    const auto &blocker = *request.receipt_persistence_blocker;
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "receipt_persistence_blocker message must not be empty");
    }

    if (blocker.required_capability.has_value() &&
        request.next_required_adapter_capability != blocker.required_capability) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "receipt_persistence_blocker required_capability must match "
                              "next_required_adapter_capability");
    }
}

} // namespace

PersistenceRequestValidationResult validate_persistence_request(const PersistenceRequest &request) {
    PersistenceRequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (request.format_version != kPersistenceRequestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request format_version must be '" +
                std::string(kPersistenceRequestFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_receipt_format_version !=
        kReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request "
            "source_durable_store_import_decision_receipt_format_version must be '" +
                std::string(kReceiptFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_format_version != kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_durable_store_import_decision_format_version must be '" +
                                  std::string(kDecisionFormatVersion) + "'");
    }

    if (request.source_durable_store_import_request_format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_durable_store_import_request_format_version must be '" +
                                  std::string(kRequestFormatVersion) + "'");
    }

    if (request.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_store_import_descriptor_format_version must be '" +
                                  std::string(store_import::kStoreImportDescriptorFormatVersion) +
                                  "'");
    }

    if (request.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_execution_plan_format_version must be '" +
                                  std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (request.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_runtime_session_format_version must be '" +
                                  std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (request.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_execution_journal_format_version must be '" +
                                  std::string(execution_journal::kExecutionJournalFormatVersion) +
                                  "'");
    }

    if (request.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_replay_view_format_version must be '" +
                                  std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (request.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_scheduler_snapshot_format_version must be '" +
                                  std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) +
                                  "'");
    }

    if (request.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "source_checkpoint_record_format_version must be '" +
                                  std::string(checkpoint_record::kCheckpointRecordFormatVersion) +
                                  "'");
    }

    if (request.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request "
            "source_persistence_descriptor_format_version must be '" +
                std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (request.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request "
            "source_export_manifest_format_version must be '" +
                std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request workflow_canonical_name "
            "must not be empty");
    }

    if (request.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request session_id must not be empty");
    }

    if (request.run_id.has_value() && request.run_id->empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request run_id must not be "
                              "empty when present");
    }

    if (request.input_fixture.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request input_fixture must not be empty");
    }

    if (request.export_package_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request export_package_identity "
            "must not be empty");
    }

    if (request.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "store_import_candidate_identity must not be empty");
    }

    if (request.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "durable_store_import_request_identity must not be empty");
    }

    if (request.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "durable_store_import_decision_identity must not be empty");
    }

    if (request.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "durable_store_import_receipt_identity must not be empty");
    }

    if (request.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request "
            "durable_store_import_receipt_persistence_request_identity must not be empty");
    }

    if (request.planned_durable_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request planned_durable_identity "
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
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request cannot contain "
            "receipt_persistence_blocker when accepted_for_receipt_persistence is true");
    }

    if (request.accepted_for_receipt_persistence &&
        request.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request cannot contain "
                              "next_required_adapter_capability when "
                              "accepted_for_receipt_persistence is true");
    }

    if (!request.accepted_for_receipt_persistence &&
        !request.receipt_persistence_blocker.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request must contain "
            "receipt_persistence_blocker when accepted_for_receipt_persistence is false");
    }

    validate_persistence_blocker(request, diagnostics);

    if (request.accepted_for_receipt_persistence &&
        request.receipt_persistence_boundary_kind !=
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request "
            "accepted_for_receipt_persistence requires "
            "AdapterReceiptPersistenceConsumable receipt_persistence_boundary_kind");
    }

    if (request.receipt_persistence_boundary_kind ==
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable &&
        !request.accepted_for_receipt_persistence) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request "
                              "adapter-receipt-persistence-consumable boundary requires "
                              "accepted_for_receipt_persistence");
    }

    if (request.accepted_for_receipt_persistence &&
        request.receipt_boundary_kind != ReceiptBoundaryKind::AdapterReceiptConsumable) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence request accepted persistence "
            "requires AdapterReceiptConsumable receipt_boundary_kind");
    }

    switch (request.receipt_persistence_request_status) {
    case PersistenceRequestStatus::ReadyToPersist:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::PersistReadyReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request ReadyToPersist "
                                  "status requires PersistReadyReceipt outcome");
        }
        if (!request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request ReadyToPersist "
                                  "status requires accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::ReadyForArchive) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request ReadyToPersist "
                                  "status requires ReadyForArchive receipt_status");
        }
        if (request.decision_status != DecisionStatus::Accepted) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request ReadyToPersist "
                                  "status requires Accepted decision_status");
        }
        if (request.request_status != RequestStatus::ReadyForAdapter &&
            request.request_status != RequestStatus::TerminalCompleted) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request ReadyToPersist "
                "status requires ReadyForAdapter or TerminalCompleted request_status");
        }
        break;
    case PersistenceRequestStatus::Blocked:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::BlockBlockedReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "requires BlockBlockedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Blocked) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "requires Blocked receipt_status");
        }
        if (request.decision_status != DecisionStatus::Blocked) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "requires Blocked decision_status");
        }
        if (request.request_status != RequestStatus::Blocked) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "requires Blocked request_status");
        }
        if (!request.receipt_persistence_blocker.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence request Blocked status "
                                  "requires receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Deferred:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::DeferPartialReceipt) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires DeferPartialReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Deferred) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires Deferred receipt_status");
        }
        if (request.decision_status != DecisionStatus::Deferred) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires Deferred decision_status");
        }
        if (request.request_status != RequestStatus::TerminalPartial) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires TerminalPartial request_status");
        }
        if (!request.next_required_adapter_capability.has_value() ||
            *request.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires PreservePartialWorkflowState "
                "next_required_adapter_capability");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind !=
                PersistenceBlockerKind::PartialWorkflowState) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Deferred status "
                "requires PartialWorkflowState receipt_persistence_blocker");
        }
        break;
    case PersistenceRequestStatus::Rejected:
        if (request.receipt_persistence_request_outcome !=
            PersistenceRequestOutcome::RejectFailedReceipt) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires RejectFailedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != ReceiptStatus::Rejected) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires Rejected receipt_status");
        }
        if (request.decision_status != DecisionStatus::Rejected) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires Rejected decision_status");
        }
        if (request.request_status != RequestStatus::TerminalFailed) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires TerminalFailed request_status");
        }
        if (!request.workflow_failure_summary.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires workflow_failure_summary");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind != PersistenceBlockerKind::WorkflowFailure) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence request Rejected status "
                "requires WorkflowFailure receipt_persistence_blocker");
        }
        break;
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        request.receipt_persistence_request_status != PersistenceRequestStatus::ReadyToPersist) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request completed "
                              "workflow_status requires ReadyToPersist "
                              "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        request.receipt_persistence_request_status != PersistenceRequestStatus::Rejected) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request failed "
                              "workflow_status requires Rejected "
                              "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        request.receipt_persistence_request_status != PersistenceRequestStatus::ReadyToPersist &&
        request.receipt_persistence_request_status != PersistenceRequestStatus::Blocked &&
        request.receipt_persistence_request_status != PersistenceRequestStatus::Deferred) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence request partial "
                              "workflow_status must map to ReadyToPersist, Blocked, or Deferred "
                              "receipt_persistence_request_status");
    }

    return result;
}

PersistenceRequestResult build_persistence_request(const Receipt &receipt) {
    PersistenceRequestResult result{
        .request = std::nullopt,
        .diagnostics = {},
    };

    const auto receipt_validation = validate_receipt(receipt);
    result.diagnostics.append(receipt_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    auto request = build_receipt_persistence_request_projection(receipt);

    const auto validation = validate_persistence_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

} // namespace ahfl::durable_store_import

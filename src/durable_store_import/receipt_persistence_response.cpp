#include "durable_store_import/receipt_persistence_response.hpp"
#include "durable_store_import/receipt_persistence_stage.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> kValidationDiagnosticCode{"DSI_RECEIPT_PERSISTENCE_RESPONSE"};

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(
        identity, diagnostics, "durable store import receipt persistence response");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(
        summary, diagnostics, "durable store import receipt persistence response");
}

void validate_response_blocker(const PersistenceResponse &response, DiagnosticBag &diagnostics) {
    if (!response.response_blocker.has_value()) {
        return;
    }

    const auto &blocker = *response.response_blocker;
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "response_blocker message must not be empty");
    }

    if (blocker.required_capability.has_value() &&
        response.next_required_adapter_capability != blocker.required_capability) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "response_blocker required_capability must match "
                              "next_required_adapter_capability");
    }
}

} // namespace

PersistenceResponseValidationResult
validate_persistence_response(const PersistenceResponse &response) {
    PersistenceResponseValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (response.format_version != kPersistenceResponseFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response format_version must be '" +
                std::string(kPersistenceResponseFormatVersion) + "'");
    }

    if (response.source_durable_store_import_decision_receipt_persistence_request_format_version !=
        kPersistenceRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_durable_store_import_decision_receipt_persistence_request_"
                              "format_version must be '" +
                                  std::string(kPersistenceRequestFormatVersion) + "'");
    }

    if (response.source_durable_store_import_decision_receipt_format_version !=
        kReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "source_durable_store_import_decision_receipt_format_version must be '" +
                std::string(kReceiptFormatVersion) + "'");
    }

    if (response.source_durable_store_import_decision_format_version != kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_durable_store_import_decision_format_version must be '" +
                                  std::string(kDecisionFormatVersion) + "'");
    }

    if (response.source_durable_store_import_request_format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_durable_store_import_request_format_version must be '" +
                                  std::string(kRequestFormatVersion) + "'");
    }

    if (response.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_store_import_descriptor_format_version must be '" +
                                  std::string(store_import::kStoreImportDescriptorFormatVersion) +
                                  "'");
    }

    if (response.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_execution_plan_format_version must be '" +
                                  std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (response.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_runtime_session_format_version must be '" +
                                  std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (response.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_execution_journal_format_version must be '" +
                                  std::string(execution_journal::kExecutionJournalFormatVersion) +
                                  "'");
    }

    if (response.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_replay_view_format_version must be '" +
                                  std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (response.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_scheduler_snapshot_format_version must be '" +
                                  std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) +
                                  "'");
    }

    if (response.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "source_checkpoint_record_format_version must be '" +
                                  std::string(checkpoint_record::kCheckpointRecordFormatVersion) +
                                  "'");
    }

    if (response.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "source_persistence_descriptor_format_version must be '" +
                std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (response.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "source_export_manifest_format_version must be '" +
                std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (response.workflow_canonical_name.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response workflow_canonical_name "
            "must not be empty");
    }

    if (response.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response session_id must not be empty");
    }

    if (response.run_id.has_value() && response.run_id->empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response run_id must not "
                              "be empty when present");
    }

    if (response.input_fixture.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response input_fixture must not be empty");
    }

    if (response.export_package_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response export_package_identity "
            "must not be empty");
    }

    if (response.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "store_import_candidate_identity must not be empty");
    }

    if (response.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "durable_store_import_request_identity must not be empty");
    }

    if (response.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "durable_store_import_decision_identity must not be empty");
    }

    if (response.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "durable_store_import_receipt_identity must not be empty");
    }

    if (response.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "durable_store_import_receipt_persistence_request_identity must not be empty");
    }

    if (response.durable_store_import_receipt_persistence_response_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "durable_store_import_receipt_persistence_response_identity must not be empty");
    }

    if (response.planned_durable_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response planned_durable_identity "
            "must not be empty");
    }

    if (response.source_package_identity.has_value()) {
        validate_package_identity(*response.source_package_identity, diagnostics);
    }

    if (response.workflow_failure_summary.has_value()) {
        validate_failure_summary(*response.workflow_failure_summary, diagnostics);
    }

    if (response.acknowledged_for_response && response.response_blocker.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response cannot contain "
                              "response_blocker when acknowledged_for_response is true");
    }

    if (response.acknowledged_for_response &&
        response.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response cannot contain "
                              "next_required_adapter_capability when "
                              "acknowledged_for_response is true");
    }

    if (!response.acknowledged_for_response && !response.response_blocker.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response must contain "
                              "response_blocker when acknowledged_for_response is false");
    }

    validate_response_blocker(response, diagnostics);

    if (response.acknowledged_for_response &&
        response.receipt_persistence_response_boundary_kind !=
            PersistenceResponseBoundaryKind::AdapterResponseConsumable) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response "
            "acknowledged_for_response requires "
            "AdapterResponseConsumable receipt_persistence_response_boundary_kind");
    }

    if (response.receipt_persistence_response_boundary_kind ==
            PersistenceResponseBoundaryKind::AdapterResponseConsumable &&
        !response.acknowledged_for_response) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response "
                              "adapter-response-consumable boundary requires "
                              "acknowledged_for_response");
    }

    if (response.acknowledged_for_response &&
        response.receipt_persistence_boundary_kind !=
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable) {
        emit_validation_error(
            diagnostics,
            "durable store import receipt persistence response acknowledged response "
            "requires AdapterReceiptPersistenceConsumable receipt_persistence_boundary_kind");
    }

    switch (response.response_status) {
    case PersistenceResponseStatus::Accepted:
        if (response.response_outcome != PersistenceResponseOutcome::AcceptPersistenceRequest) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence response Accepted "
                                  "status requires AcceptPersistenceRequest outcome");
        }
        if (!response.acknowledged_for_response) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence response Accepted "
                                  "status requires acknowledged_for_response");
        }
        if (response.persistence_request_status != PersistenceRequestStatus::ReadyToPersist) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence response Accepted "
                                  "status requires ReadyToPersist persistence_request_status");
        }
        if (response.receipt_status != ReceiptStatus::ReadyForArchive) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence response Accepted "
                                  "status requires ReadyForArchive receipt_status");
        }
        if (response.decision_status != DecisionStatus::Accepted) {
            emit_validation_error(diagnostics,
                                  "durable store import receipt persistence response Accepted "
                                  "status requires Accepted decision_status");
        }
        if (response.request_status != RequestStatus::ReadyForAdapter &&
            response.request_status != RequestStatus::TerminalCompleted) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Accepted "
                "status requires ReadyForAdapter or TerminalCompleted request_status");
        }
        break;
    case PersistenceResponseStatus::Blocked:
        if (response.response_outcome != PersistenceResponseOutcome::BlockBlockedRequest) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires BlockBlockedRequest outcome");
        }
        if (response.acknowledged_for_response) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "cannot be acknowledged_for_response");
        }
        if (response.persistence_request_status != PersistenceRequestStatus::Blocked) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires Blocked persistence_request_status");
        }
        if (response.receipt_status != ReceiptStatus::Blocked) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires Blocked receipt_status");
        }
        if (response.decision_status != DecisionStatus::Blocked) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires Blocked decision_status");
        }
        if (response.request_status != RequestStatus::Blocked) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires Blocked request_status");
        }
        if (!response.response_blocker.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Blocked status "
                "requires response_blocker");
        }
        break;
    case PersistenceResponseStatus::Deferred:
        if (response.response_outcome != PersistenceResponseOutcome::DeferDeferredRequest) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires DeferDeferredRequest outcome");
        }
        if (response.acknowledged_for_response) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "cannot be acknowledged_for_response");
        }
        if (response.persistence_request_status != PersistenceRequestStatus::Deferred) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires Deferred persistence_request_status");
        }
        if (response.receipt_status != ReceiptStatus::Deferred) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires Deferred receipt_status");
        }
        if (response.decision_status != DecisionStatus::Deferred) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires Deferred decision_status");
        }
        if (response.request_status != RequestStatus::TerminalPartial) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires TerminalPartial request_status");
        }
        if (!response.next_required_adapter_capability.has_value() ||
            *response.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires PreservePartialWorkflowState "
                "next_required_adapter_capability");
        }
        if (!response.response_blocker.has_value() ||
            response.response_blocker->kind !=
                PersistenceResponseBlockerKind::PartialPersistenceState) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Deferred status "
                "requires PartialPersistenceState response_blocker");
        }
        break;
    case PersistenceResponseStatus::Rejected:
        if (response.response_outcome != PersistenceResponseOutcome::RejectFailedRequest) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires RejectFailedRequest outcome");
        }
        if (response.acknowledged_for_response) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "cannot be acknowledged_for_response");
        }
        if (response.persistence_request_status != PersistenceRequestStatus::Rejected) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires Rejected persistence_request_status");
        }
        if (response.receipt_status != ReceiptStatus::Rejected) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires Rejected receipt_status");
        }
        if (response.decision_status != DecisionStatus::Rejected) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires Rejected decision_status");
        }
        if (response.request_status != RequestStatus::TerminalFailed) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires TerminalFailed request_status");
        }
        if (!response.workflow_failure_summary.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires workflow_failure_summary");
        }
        if (!response.response_blocker.has_value() ||
            response.response_blocker->kind !=
                PersistenceResponseBlockerKind::PersistenceWorkflowFailure) {
            emit_validation_error(
                diagnostics,
                "durable store import receipt persistence response Rejected status "
                "requires PersistenceWorkflowFailure response_blocker");
        }
        break;
    }

    if (response.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        response.response_status != PersistenceResponseStatus::Accepted) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response completed "
                              "workflow_status requires Accepted "
                              "response_status");
    }

    if (response.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        response.response_status != PersistenceResponseStatus::Rejected) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response failed "
                              "workflow_status requires Rejected "
                              "response_status");
    }

    if (response.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        response.response_status != PersistenceResponseStatus::Accepted &&
        response.response_status != PersistenceResponseStatus::Blocked &&
        response.response_status != PersistenceResponseStatus::Deferred) {
        emit_validation_error(diagnostics,
                              "durable store import receipt persistence response partial "
                              "workflow_status must map to Accepted, Blocked, or Deferred "
                              "response_status");
    }

    return result;
}

PersistenceResponseResult build_persistence_response(const PersistenceRequest &request) {
    PersistenceResponseResult result{
        .response = std::nullopt,
        .diagnostics = {},
    };

    const auto request_validation = validate_persistence_request(request);
    result.diagnostics.append(request_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    auto response = build_receipt_persistence_response_projection(request);

    const auto validation = validate_persistence_response(response);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.response = std::move(response);
    return result;
}

} // namespace ahfl::durable_store_import
